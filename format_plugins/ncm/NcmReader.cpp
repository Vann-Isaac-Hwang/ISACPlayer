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
