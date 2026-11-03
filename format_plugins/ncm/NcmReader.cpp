#include "NcmReader.h"

#include "aes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace isac::format::ncm {
namespace {

constexpr std::array<unsigned char, 17> kCoreKey{
    0x68, 0x7A, 0x48, 0x52, 0x41, 0x6D, 0x73, 0x6F, 0x35,
    0x6B, 0x49, 0x6E, 0x62, 0x61, 0x78, 0x57, 0x00};
constexpr std::array<unsigned char, 17> kModifyKey{
    0x23, 0x31, 0x34, 0x6C, 0x6A, 0x6B, 0x5F, 0x21, 0x5C,
    0x5D, 0x26, 0x30, 0x55, 0x3C, 0x27, 0x28, 0x00};

bool readUInt32(QFile& file, quint32& value)
{
    quint32 littleEndian = 0;
    if (file.read(reinterpret_cast<char*>(&littleEndian),
                  sizeof(littleEndian)) != sizeof(littleEndian)) {
        return false;
    }
    value = qFromLittleEndian(littleEndian);
    return true;
}

bool readBlock(QFile& file, quint32 length, QByteArray& data)
{
    constexpr quint32 kMaximumBlockSize = 64 * 1024 * 1024;
    if (length == 0 || length > kMaximumBlockSize) {
        return false;
    }
    data = file.read(static_cast<qint64>(length));
    return data.size() == static_cast<qsizetype>(length);
}

QByteArray decryptEcb(const QByteArray& input,
                      const std::array<unsigned char, 17>& key)
{
    if (input.isEmpty() || input.size() % 16 != 0) {
        return {};
    }

    AES aes(key.data());
    QByteArray output;
    output.resize(input.size());
    for (int offset = 0; offset < input.size(); offset += 16) {
        unsigned char block[16]{};
        unsigned char decrypted[16]{};
        std::memcpy(block, input.constData() + offset, sizeof(block));
        aes.decrypt(block, decrypted);
        std::memcpy(output.data() + offset, decrypted, sizeof(decrypted));
    }

    const unsigned char padding =
        static_cast<unsigned char>(output.constData()[output.size() - 1]);
    if (padding > 0 && padding <= 16) {
        output.chop(static_cast<int>(padding));
    }
    return output;
}

void buildKeyBox(const QByteArray& key, std::array<unsigned char, 256>& box)
{
    for (int i = 0; i < 256; ++i) {
        box[static_cast<std::size_t>(i)] =
            static_cast<unsigned char>(i);
    }

    unsigned char lastByte = 0;
    unsigned char keyOffset = 0;
    for (int i = 0; i < 256; ++i) {
        const unsigned char swap = box[static_cast<std::size_t>(i)];
        const unsigned char c = static_cast<unsigned char>(
            (swap + lastByte +
             static_cast<unsigned char>(key[keyOffset])) & 0xff);
        ++keyOffset;
        if (keyOffset >= key.size()) {
            keyOffset = 0;
        }
        box[static_cast<std::size_t>(i)] =
            box[static_cast<std::size_t>(c)];
        box[static_cast<std::size_t>(c)] = swap;
        lastByte = c;
    }
}

int jsonInteger(const QJsonObject& object,
                const QStringList& keys)
{
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) {
            const int result = value.toInt();
            if (result > 0) {
                return result;
            }
        }
        if (value.isString()) {
            bool ok = false;
            const int result = value.toString().toInt(&ok);
            if (ok && result > 0) {
                return result;
            }
        }
    }
    return 0;
}

QByteArray decodePayloadRange(
    QFile& file, const std::array<unsigned char, 256>& keyBox,
    qint64 payloadStart, quint64 offset, quint32 length)
{
    if (length == 0 ||
        length > static_cast<quint32>(std::numeric_limits<int>::max())) {
        return {};
    }

    const qint64 payloadSize = file.size() - payloadStart;
    if (payloadSize < 0 ||
        offset > static_cast<quint64>(payloadSize) ||
        length > static_cast<quint64>(payloadSize) - offset ||
        !file.seek(payloadStart + static_cast<qint64>(offset))) {
        return {};
    }

    QByteArray decoded = file.read(static_cast<qint64>(length));
    if (decoded.size() != static_cast<int>(length)) {
        return {};
    }
    for (int i = 0; i < decoded.size(); ++i) {
        const quint64 streamOffset = offset + static_cast<quint64>(i);
        const int j = static_cast<int>((streamOffset + 1) & 0xff);
        const int index =
            (keyBox[static_cast<std::size_t>(j)] +
             keyBox[static_cast<std::size_t>(
                 (keyBox[static_cast<std::size_t>(j)] + j) & 0xff)]) &
            0xff;
        decoded[i] = static_cast<char>(
            static_cast<unsigned char>(decoded.at(i)) ^
            keyBox[static_cast<std::size_t>(index)]);
    }
    return decoded;
}

bool readCommentUInt32(const QByteArray& data, int& position,
                       quint32& value)
{
    if (position < 0 || position > data.size() - 4) {
        return false;
    }
    quint32 littleEndian = 0;
    std::memcpy(&littleEndian, data.constData() + position,
                sizeof(littleEndian));
    value = qFromLittleEndian(littleEndian);
    position += 4;
    return true;
}

int parsePositionNumber(QString value)
{
    value = value.trimmed();
    const int slash = value.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        value.truncate(slash);
    }
    bool ok = false;
    const int number = value.trimmed().toInt(&ok);
    return ok && number > 0 ? number : 0;
}

void readVorbisPosition(const QByteArray& comment,
                        int& trackNumber, int& discNumber)
{
    const int separator = comment.indexOf('=');
    if (separator <= 0) {
        return;
    }
    const QByteArray key = comment.left(separator).toUpper();
    const QString value = QString::fromUtf8(
        comment.constData() + separator + 1,
        comment.size() - separator - 1);
    if (key == QByteArrayLiteral("TRACKNUMBER") ||
        key == QByteArrayLiteral("TRACK")) {
        const int number = parsePositionNumber(value);
        if (number > 0) {
            trackNumber = number;
        }
    } else if (key == QByteArrayLiteral("DISCNUMBER") ||
               key == QByteArrayLiteral("DISC")) {
        const int number = parsePositionNumber(value);
        if (number > 0) {
            discNumber = number;
        }
    }
}

void parseVorbisComments(const QByteArray& block,
                         int& trackNumber, int& discNumber)
{
    int position = 0;
    quint32 vendorLength = 0;
    if (!readCommentUInt32(block, position, vendorLength) ||
        vendorLength > static_cast<quint32>(block.size() - position)) {
        return;
    }
    position += static_cast<int>(vendorLength);

    quint32 commentCount = 0;
    if (!readCommentUInt32(block, position, commentCount) ||
        commentCount > 100000) {
        return;
    }
    for (quint32 i = 0; i < commentCount; ++i) {
        quint32 commentLength = 0;
        if (!readCommentUInt32(block, position, commentLength) ||
            commentLength > static_cast<quint32>(block.size() - position)) {
            return;
        }
        readVorbisPosition(
            block.mid(position, static_cast<int>(commentLength)),
            trackNumber, discNumber);
        position += static_cast<int>(commentLength);
    }
}

bool readFlacPositionTags(QFile& file,
                          const std::array<unsigned char, 256>& keyBox,
                          qint64 payloadStart,
                          int& trackNumber, int& discNumber)
{
    if (decodePayloadRange(file, keyBox, payloadStart, 0, 4) !=
        QByteArrayLiteral("fLaC")) {
        return false;
    }

    const qint64 payloadSize = file.size() - payloadStart;
    quint64 offset = 4;
    for (int blockIndex = 0; blockIndex < 1024; ++blockIndex) {
        const QByteArray header =
            decodePayloadRange(file, keyBox, payloadStart, offset, 4);
        if (header.size() != 4) {
            return false;
        }

        const unsigned char first =
            static_cast<unsigned char>(header.at(0));
        const bool isLast = (first & 0x80) != 0;
        const int blockType = first & 0x7f;
        const quint32 blockLength =
            (static_cast<quint32>(
                 static_cast<unsigned char>(header.at(1))) << 16) |
            (static_cast<quint32>(
                 static_cast<unsigned char>(header.at(2))) << 8) |
            static_cast<quint32>(
                static_cast<unsigned char>(header.at(3)));
        offset += 4;
        if (payloadSize < 0 ||
            offset > static_cast<quint64>(payloadSize) ||
            blockLength > static_cast<quint64>(payloadSize) - offset) {
            return false;
        }

        if (blockType == 4) {
            const QByteArray comments = decodePayloadRange(
                file, keyBox, payloadStart, offset, blockLength);
            if (comments.size() != static_cast<int>(blockLength)) {
                return false;
            }
            parseVorbisComments(comments, trackNumber, discNumber);
            if (trackNumber > 0 && discNumber > 0) {
                return true;
            }
        }
        offset += blockLength;
        if (isLast) {
            return true;
        }
    }
    return false;
}

bool readSyncSafeUInt32(const QByteArray& bytes, int offset, quint32& value)
{
    if (offset < 0 || offset > bytes.size() - 4) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned char byte =
            static_cast<unsigned char>(bytes.at(offset + i));
        if ((byte & 0x80) != 0) {
            return false;
        }
        value = (value << 7) | byte;
    }
    return true;
}

QString decodeId3Text(const QByteArray& payload)
{
    if (payload.isEmpty()) {
        return {};
    }
    const unsigned char encoding =
        static_cast<unsigned char>(payload.at(0));
    QByteArray text = payload.mid(1);
    QString result;
    if (encoding == 0) {
        result = QString::fromLatin1(text);
    } else if (encoding == 3) {
        result = QString::fromUtf8(text);
    } else if (encoding == 1 || encoding == 2) {
        bool littleEndian = false;
        if (encoding == 1 && text.size() >= 2) {
            const unsigned char first =
                static_cast<unsigned char>(text.at(0));
            const unsigned char second =
                static_cast<unsigned char>(text.at(1));
            if (first == 0xff && second == 0xfe) {
                littleEndian = true;
                text.remove(0, 2);
            } else if (first == 0xfe && second == 0xff) {
                text.remove(0, 2);
            }
        }
        for (int i = 0; i + 1 < text.size(); i += 2) {
            const auto* bytes = reinterpret_cast<const uchar*>(
                text.constData() + i);
            const quint16 code = littleEndian
                ? qFromLittleEndian<quint16>(bytes)
                : qFromBigEndian<quint16>(bytes);
            if (code == 0) {
                break;
            }
            result.append(QChar(code));
        }
    }
    const int terminator = result.indexOf(QChar::Null);
    if (terminator >= 0) {
        result.truncate(terminator);
    }
    return result.trimmed();
}

bool readMp3PositionTags(QFile& file,
                         const std::array<unsigned char, 256>& keyBox,
                         qint64 payloadStart,
                         int& trackNumber, int& discNumber)
{
    const QByteArray header =
        decodePayloadRange(file, keyBox, payloadStart, 0, 10);
    if (header.size() != 10 || header.left(3) != QByteArrayLiteral("ID3")) {
        return false;
    }

    const int majorVersion =
        static_cast<unsigned char>(header.at(3));
    if (majorVersion < 2 || majorVersion > 4) {
        return false;
    }
    quint32 tagLength = 0;
    if (!readSyncSafeUInt32(header, 6, tagLength) ||
        tagLength > 64 * 1024 * 1024) {
        return false;
    }

    const qint64 payloadSize = file.size() - payloadStart;
    if (payloadSize < 10 ||
        tagLength > static_cast<quint64>(payloadSize - 10)) {
        return false;
    }
    const quint64 tagEnd = 10 + static_cast<quint64>(tagLength);
    quint64 offset = 10;
    const unsigned char tagFlags =
        static_cast<unsigned char>(header.at(5));

    if (majorVersion >= 3 && (tagFlags & 0x40) != 0) {
        const QByteArray extendedSize =
            decodePayloadRange(file, keyBox, payloadStart, offset, 4);
        if (extendedSize.size() != 4) {
            return false;
        }
        quint32 size = 0;
        if (majorVersion == 4) {
            if (!readSyncSafeUInt32(extendedSize, 0, size)) {
                return false;
            }
            offset += size;
        } else {
            size =
                (static_cast<quint32>(
                     static_cast<unsigned char>(extendedSize.at(0))) << 24) |
                (static_cast<quint32>(
                     static_cast<unsigned char>(extendedSize.at(1))) << 16) |
                (static_cast<quint32>(
                     static_cast<unsigned char>(extendedSize.at(2))) << 8) |
                static_cast<quint32>(
                    static_cast<unsigned char>(extendedSize.at(3)));
            offset += 4 + size;
        }
        if (offset > tagEnd) {
            return false;
        }
    }

    const int frameHeaderSize = majorVersion == 2 ? 6 : 10;
    for (int frameIndex = 0;
         frameIndex < 1024 && offset + frameHeaderSize <= tagEnd;
         ++frameIndex) {
        const QByteArray frameHeader = decodePayloadRange(
            file, keyBox, payloadStart, offset,
            static_cast<quint32>(frameHeaderSize));
        if (frameHeader.size() != frameHeaderSize ||
            frameHeader.at(0) == '\0') {
            break;
        }

        const QByteArray frameId = frameHeader.left(
            majorVersion == 2 ? 3 : 4);
        quint32 frameLength = 0;
        if (majorVersion == 2) {
            frameLength =
                (static_cast<quint32>(
                     static_cast<unsigned char>(frameHeader.at(3))) << 16) |
                (static_cast<quint32>(
                     static_cast<unsigned char>(frameHeader.at(4))) << 8) |
                static_cast<quint32>(
                    static_cast<unsigned char>(frameHeader.at(5)));
        } else if (majorVersion == 4) {
            if (!readSyncSafeUInt32(frameHeader, 4, frameLength)) {
                return false;
            }
        } else {
            frameLength =
                (static_cast<quint32>(
                     static_cast<unsigned char>(frameHeader.at(4))) << 24) |
                (static_cast<quint32>(
                     static_cast<unsigned char>(frameHeader.at(5))) << 16) |
                (static_cast<quint32>(
                     static_cast<unsigned char>(frameHeader.at(6))) << 8) |
                static_cast<quint32>(
                    static_cast<unsigned char>(frameHeader.at(7)));
        }
        offset += static_cast<quint64>(frameHeaderSize);
        if (frameLength > tagEnd - offset) {
            return false;
        }

        const bool isTrackFrame =
            frameId == QByteArrayLiteral("TRK") ||
            frameId == QByteArrayLiteral("TRCK");
        const bool isDiscFrame =
            frameId == QByteArrayLiteral("TPA") ||
            frameId == QByteArrayLiteral("TPOS");
        if ((isTrackFrame || isDiscFrame) &&
            frameLength > 0 && frameLength <= 4096) {
            const QByteArray payload = decodePayloadRange(
                file, keyBox, payloadStart, offset, frameLength);
            const int number =
                parsePositionNumber(decodeId3Text(payload));
            if (number > 0) {
                (isTrackFrame ? trackNumber : discNumber) = number;
            }
        }
        offset += frameLength;
        if (trackNumber > 0 && discNumber > 0) {
            return true;
        }
    }
    return true;
}

void inferTrackPosition(const QString& filePath, FormatMetadata& metadata)
{
    const QString fileName = QFileInfo(filePath).completeBaseName();

    static const QRegularExpression discTrackPattern(
        QStringLiteral("^\\s*[dD](\\d{1,2})\\s*[-_. ]\\s*[tT]?(\\d{1,3})(?:\\D|$)"));
    const auto discTrackMatch = discTrackPattern.match(fileName);
    if (discTrackMatch.hasMatch()) {
        metadata.discNumber = discTrackMatch.captured(1).toInt();
        metadata.trackNumber = discTrackMatch.captured(2).toInt();
        return;
    }

    static const QRegularExpression numericDiscTrackPattern(
        QStringLiteral("^\\s*(\\d{1,2})\\s*[-_.]\\s*[tT]?(\\d{1,3})(?:\\D|$)"));
    const auto numericDiscTrackMatch =
        numericDiscTrackPattern.match(fileName);
    if (numericDiscTrackMatch.hasMatch()) {
        metadata.discNumber = numericDiscTrackMatch.captured(1).toInt();
        metadata.trackNumber = numericDiscTrackMatch.captured(2).toInt();
        return;
    }

    static const QRegularExpression trackPattern(
        QStringLiteral("^\\s*(\\d{1,3})(?:\\s*[-_. )]|\\s+|$)"));
    const auto trackMatch = trackPattern.match(fileName);
    if (trackMatch.hasMatch()) {
        metadata.trackNumber = trackMatch.captured(1).toInt();
    }
}

bool parseHeader(QFile& file, std::array<unsigned char, 256>& keyBox,
                 FormatMetadata* metadata, QByteArray* coverData,
                 QString* error)
{
    if (file.read(8) != QByteArrayLiteral("CTENFDAM") ||
        !file.seek(10)) {
        if (error) {
            *error = QStringLiteral("Invalid NCM header");
        }
        return false;
    }

    quint32 keyLength = 0;
    if (!readUInt32(file, keyLength)) {
        if (error) {
            *error = QStringLiteral("Missing NCM key section");
        }
        return false;
    }
    QByteArray keyData;
    if (!readBlock(file, keyLength, keyData)) {
        if (error) {
            *error = QStringLiteral("Invalid NCM key section");
        }
        return false;
    }
    for (char& value : keyData) {
        value = static_cast<char>(
            static_cast<unsigned char>(value) ^ 0x64);
    }
    const QByteArray decryptedKey = decryptEcb(keyData, kCoreKey);
    if (decryptedKey.size() <= 17) {
        if (error) {
            *error = QStringLiteral("Unable to decode NCM key");
        }
        return false;
    }
    buildKeyBox(decryptedKey.mid(17), keyBox);

    quint32 metadataLength = 0;
    if (!readUInt32(file, metadataLength)) {
        if (error) {
            *error = QStringLiteral("Missing NCM metadata section");
        }
        return false;
    }

    QByteArray metadataData;
    if (metadataLength > 0) {
        if (!readBlock(file, metadataLength, metadataData)) {
            if (error) {
                *error = QStringLiteral("Invalid NCM metadata section");
            }
            return false;
        }
        for (char& value : metadataData) {
            value = static_cast<char>(
                static_cast<unsigned char>(value) ^ 0x63);
        }
        if (metadataData.size() > 22) {
            const QByteArray encoded = metadataData.mid(22);
            const QByteArray decoded = QByteArray::fromBase64(encoded);
            QByteArray decrypted = decryptEcb(decoded, kModifyKey);
            if (decrypted.size() > 6) {
                decrypted.remove(0, 6);
                const QJsonDocument document =
                    QJsonDocument::fromJson(decrypted);
                const QJsonObject object = document.object();
                if (metadata) {
                    metadata->title =
                        object.value(QStringLiteral("musicName")).toString();
                    metadata->album =
                        object.value(QStringLiteral("album")).toString();
                    const QJsonValue albumId =
                        object.value(QStringLiteral("albumId"));
                    metadata->albumId = albumId.isString()
                        ? albumId.toString()
                        : albumId.toVariant().toString();
                    const QJsonArray artists =
                        object.value(QStringLiteral("artist")).toArray();
                    QStringList artistNames;
                    for (const QJsonValue& artistValue : artists) {
                        const QJsonArray artist = artistValue.toArray();
                        if (!artist.isEmpty()) {
                            const QString name = artist.first().toString();
                            if (!name.isEmpty()) {
                                artistNames.push_back(name);
                            }
                        }
                    }
                    metadata->artist = artistNames.join(QStringLiteral("/"));
                    metadata->durationMilliseconds =
                        object.value(QStringLiteral("duration")).toInt();
                    metadata->bitrateKbps =
                        object.value(QStringLiteral("bitrate")).toInt() / 1000;
                    metadata->decodedFormat =
                        object.value(QStringLiteral("format")).toString()
                            .toLower();
                    metadata->trackNumber = jsonInteger(
                        object,
                        {QStringLiteral("trackNumber"),
                         QStringLiteral("trackNum"),
                         QStringLiteral("track"),
                         QStringLiteral("track_no"),
                         QStringLiteral("trackIndex"),
                         QStringLiteral("track_index")});
                    metadata->discNumber = jsonInteger(
                        object,
                        {QStringLiteral("discNumber"),
                         QStringLiteral("discNum"),
                         QStringLiteral("disc"),
                         QStringLiteral("disc_no"),
                         QStringLiteral("discIndex"),
                         QStringLiteral("disc_index")});
                }
            }
        }
    } else if (!file.seek(file.pos() + 5)) {
        if (error) {
            *error = QStringLiteral("Invalid NCM metadata offset");
        }
        return false;
    }

    if (metadataLength > 0 && !file.seek(file.pos() + 5)) {
        if (error) {
            *error = QStringLiteral("Invalid NCM cover offset");
        }
        return false;
    }

    quint32 coverFrameLength = 0;
    quint32 coverLength = 0;
    if (!readUInt32(file, coverFrameLength) ||
        !readUInt32(file, coverLength)) {
        if (error) {
            *error = QStringLiteral("Missing NCM cover section");
        }
        return false;
    }
    if (coverLength > coverFrameLength ||
        coverLength > 64 * 1024 * 1024) {
        if (error) {
            *error = QStringLiteral("Invalid NCM cover section");
        }
        return false;
    }
    if (coverLength > 0) {
        if (!readBlock(file, coverLength, *coverData)) {
            if (error) {
                *error = QStringLiteral("Unable to read NCM cover");
            }
            return false;
        }
    }
    if (!file.seek(file.pos() + coverFrameLength - coverLength)) {
        if (error) {
            *error = QStringLiteral("Invalid NCM audio offset");
        }
        return false;
    }
    if (metadata && metadata->trackNumber <= 0) {
        inferTrackPosition(file.fileName(), *metadata);
    }
    return true;
}

bool decodeAudio(QFile& input, QIODevice& output,
                 const std::array<unsigned char, 256>& keyBox,
                 QString* error)
{
    QByteArray encrypted(32 * 1024, '\0');
    bool wroteData = false;
    while (!input.atEnd()) {
        const qint64 bytesRead = input.read(encrypted.data(), encrypted.size());
        if (bytesRead < 0) {
            if (error) {
                *error = input.errorString();
            }
            return false;
        }
        if (bytesRead == 0) {
            break;
        }

        QByteArray decoded = encrypted.left(static_cast<int>(bytesRead));
        for (int i = 0; i < decoded.size(); ++i) {
            const int j = (i + 1) & 0xff;
            const int index =
                (keyBox[static_cast<std::size_t>(j)] +
                 keyBox[static_cast<std::size_t>(
                     (keyBox[static_cast<std::size_t>(j)] + j) & 0xff)]) &
                0xff;
            decoded[i] = static_cast<char>(
                static_cast<unsigned char>(decoded.at(i)) ^
                keyBox[static_cast<std::size_t>(index)]);
        }
        if (output.write(decoded) != decoded.size()) {
            if (error) {
                *error = output.errorString();
            }
            return false;
        }
        wroteData = true;
    }
    if (!wroteData && error) {
        *error = QStringLiteral("NCM contains no audio payload");
    }
    return wroteData;
}

} // namespace

FormatMetadata NcmReader::inspect(const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }

    std::array<unsigned char, 256> keyBox{};
    FormatMetadata metadata;
    QByteArray coverData;
    if (!parseHeader(file, keyBox, &metadata, &coverData, error)) {
        return {};
    }
    const qint64 payloadStart = file.pos();
    int payloadTrackNumber = 0;
    int payloadDiscNumber = 0;
    if (metadata.decodedFormat == QStringLiteral("flac")) {
        readFlacPositionTags(file, keyBox, payloadStart,
                             payloadTrackNumber, payloadDiscNumber);
    } else if (metadata.decodedFormat == QStringLiteral("mp3")) {
        readMp3PositionTags(file, keyBox, payloadStart,
                            payloadTrackNumber, payloadDiscNumber);
    }
    if (payloadTrackNumber > 0) {
        metadata.trackNumber = payloadTrackNumber;
    }
    if (payloadDiscNumber > 0) {
        metadata.discNumber = payloadDiscNumber;
    }
    metadata.metadataRevision = 1;
    metadata.valid = true;
    metadata.coverData = std::move(coverData);
    return metadata;
}

bool NcmReader::materialize(const QString& sourcePath,
                            const QString& outputPath,
                            QString* error)
{
    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = input.errorString();
        }
        return false;
    }

    std::array<unsigned char, 256> keyBox{};
    FormatMetadata metadata;
    QByteArray coverData;
    if (!parseHeader(input, keyBox, &metadata, &coverData, error)) {
        return false;
    }

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    if (!decodeAudio(input, output, keyBox, error) || !output.commit()) {
        if (error && error->isEmpty()) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

} // namespace isac::format::ncm
