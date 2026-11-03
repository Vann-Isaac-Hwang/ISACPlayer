#include "NcmReader.h"
#include "aes.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::array<unsigned char, 16> kCoreKey{
    0x68, 0x7a, 0x48, 0x52, 0x41, 0x6d, 0x73, 0x6f,
    0x35, 0x6b, 0x49, 0x6e, 0x62, 0x61, 0x78, 0x57};
constexpr std::array<unsigned char, 16> kModifyKey{
    0x23, 0x31, 0x34, 0x6c, 0x6a, 0x6b, 0x5f, 0x21,
    0x5c, 0x5d, 0x26, 0x30, 0x55, 0x3c, 0x27, 0x28};
const QByteArray kRawKey = QByteArrayLiteral("0123456789abcdef");

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QByteArray pkcs7(QByteArray input)
{
    const int padding = 16 - input.size() % 16;
    input.append(QByteArray(padding, static_cast<char>(padding)));
    return input;
}

QByteArray encryptEcb(const QByteArray& input,
                      const std::array<unsigned char, 16>& key)
{
    expect(input.size() % 16 == 0, "AES input must be block aligned");
    AES aes(key.data());
    QByteArray output(input.size(), '\0');
    for (int offset = 0; offset < input.size(); offset += 16) {
        unsigned char block[16]{};
        unsigned char encrypted[16]{};
        std::memcpy(block, input.constData() + offset, sizeof(block));
        aes.encrypt(block, encrypted);
        std::memcpy(output.data() + offset, encrypted, sizeof(encrypted));
    }
    return output;
}

void appendUInt32Le(QByteArray& output, quint32 value)
{
    const quint32 littleEndian = qToLittleEndian(value);
    output.append(reinterpret_cast<const char*>(&littleEndian),
                  sizeof(littleEndian));
}

void appendUInt24Be(QByteArray& output, quint32 value)
{
    output.append(static_cast<char>((value >> 16) & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
    output.append(static_cast<char>(value & 0xff));
}

void appendSyncSafe32(QByteArray& output, quint32 value)
{
    output.append(static_cast<char>((value >> 21) & 0x7f));
    output.append(static_cast<char>((value >> 14) & 0x7f));
    output.append(static_cast<char>((value >> 7) & 0x7f));
    output.append(static_cast<char>(value & 0x7f));
}

QByteArray makeFlacPayload()
{
    QByteArray output = QByteArrayLiteral("fLaC");
    output.append(char(0));
    appendUInt24Be(output, 34);
    output.append(QByteArray(34, '\0'));

    QByteArray comments;
    const QByteArray vendor = QByteArrayLiteral("NcmReaderTest");
    appendUInt32Le(comments, static_cast<quint32>(vendor.size()));
    comments.append(vendor);

    const QList<QByteArray> tags{
        QByteArrayLiteral("TITLE=Probe Track"),
        QByteArrayLiteral("TRACKNUMBER=2/10"),
        QByteArrayLiteral("DISCNUMBER=2/2")
    };
    appendUInt32Le(comments, static_cast<quint32>(tags.size()));
    for (const QByteArray& tag : tags) {
        appendUInt32Le(comments, static_cast<quint32>(tag.size()));
        comments.append(tag);
    }

    output.append(static_cast<char>(0x84));
    appendUInt24Be(output, static_cast<quint32>(comments.size()));
    output.append(comments);
    output.append(QByteArrayLiteral("\xff\xf8"));
    return output;
}

QByteArray makeMp3Payload()
{
    QByteArray frames;
    const auto appendTextFrame = [&frames](
        const QByteArray& id, const QByteArray& value) {
        frames.append(id);
        appendSyncSafe32(
            frames, static_cast<quint32>(value.size() + 1));
        frames.append(QByteArray(2, '\0'));
        frames.append(char(3));
        frames.append(value);
    };
    appendTextFrame(QByteArrayLiteral("TRCK"), QByteArrayLiteral("2/12"));
    appendTextFrame(QByteArrayLiteral("TPOS"), QByteArrayLiteral("2/2"));

    QByteArray output = QByteArrayLiteral("ID3");
    output.append(char(4));
    output.append(char(0));
    output.append(char(0));
    appendSyncSafe32(output, static_cast<quint32>(frames.size()));
    output.append(frames);
    output.append(QByteArrayLiteral("\xff\xfb"));
    return output;
}

std::array<unsigned char, 256> makeKeyBox()
{
    std::array<unsigned char, 256> box{};
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
             static_cast<unsigned char>(kRawKey.at(keyOffset))) & 0xff);
        ++keyOffset;
        if (keyOffset >= kRawKey.size()) {
            keyOffset = 0;
        }
        box[static_cast<std::size_t>(i)] =
            box[static_cast<std::size_t>(c)];
        box[static_cast<std::size_t>(c)] = swap;
        lastByte = c;
    }
    return box;
}

QByteArray encryptPayload(const QByteArray& payload)
{
    const auto keyBox = makeKeyBox();
    QByteArray encrypted = payload;
    for (int i = 0; i < encrypted.size(); ++i) {
        const int j = (i + 1) & 0xff;
        const int index =
            (keyBox[static_cast<std::size_t>(j)] +
             keyBox[static_cast<std::size_t>(
                 (keyBox[static_cast<std::size_t>(j)] + j) & 0xff)]) &
            0xff;
        encrypted[i] = static_cast<char>(
            static_cast<unsigned char>(encrypted.at(i)) ^
            keyBox[static_cast<std::size_t>(index)]);
    }
    return encrypted;
}

QByteArray makeNcmFile(const QByteArray& audioPayload,
                       const QByteArray& format)
{
    QByteArray keyPlain = QByteArrayLiteral("neteasecloudmusic") + kRawKey;
    QByteArray keyData = encryptEcb(pkcs7(keyPlain), kCoreKey);
    for (char& byte : keyData) {
        byte = static_cast<char>(
            static_cast<unsigned char>(byte) ^ 0x64);
    }

    const QByteArray json = QByteArrayLiteral(
        "{\"album\":\"Probe Album\",\"albumId\":\"ncm-album-77\","
        "\"artist\":[[\"Probe Artist\",\"1\"]],"
        "\"duration\":120000,\"format\":\"") +
        format + QByteArrayLiteral("\",\"musicName\":\"Probe Track\"}");
    QByteArray metadata = QByteArrayLiteral("163 key(Don't modify):");
    metadata.append(encryptEcb(
        pkcs7(QByteArrayLiteral("music:") + json), kModifyKey).toBase64());
    for (char& byte : metadata) {
        byte = static_cast<char>(
            static_cast<unsigned char>(byte) ^ 0x63);
    }

    QByteArray output = QByteArrayLiteral("CTENFDAM");
    output.append(QByteArray(2, '\0'));
    appendUInt32Le(output, static_cast<quint32>(keyData.size()));
    output.append(keyData);
    appendUInt32Le(output, static_cast<quint32>(metadata.size()));
    output.append(metadata);
    output.append(QByteArray(5, '\0'));
    appendUInt32Le(output, 0);
    appendUInt32Le(output, 0);
    output.append(encryptPayload(audioPayload));
    return output;
}

void testNcmPayloadTrackMetadata(const QString& path,
                                 const QByteArray& format,
                                 const QByteArray& payload)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly),
           "temporary NCM fixture must open for writing");
    const QByteArray encoded = makeNcmFile(payload, format);
    expect(file.write(encoded) == encoded.size(),
           "temporary NCM fixture must be written completely");
    file.close();

    QString error;
    const auto metadata = isac::format::ncm::NcmReader::inspect(path, &error);
    expect(metadata.valid, "NCM fixture must be recognized");
    expect(metadata.albumId == QStringLiteral("ncm-album-77"),
           "NCM header albumId must be exposed");
    expect(metadata.trackNumber == 2,
           "track number must be parsed from encrypted audio metadata");
    expect(metadata.discNumber == 2,
           "disc number must be parsed from encrypted audio metadata");
    expect(metadata.metadataRevision == 1,
           "NCM metadata parser revision must be exposed for rescanning");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(),
           "temporary directory must be available");
    testNcmPayloadTrackMetadata(
        temporaryDirectory.filePath(QStringLiteral("probe-flac.ncm")),
        QByteArrayLiteral("flac"), makeFlacPayload());
    testNcmPayloadTrackMetadata(
        temporaryDirectory.filePath(QStringLiteral("probe-mp3.ncm")),
        QByteArrayLiteral("mp3"), makeMp3Payload());
    return EXIT_SUCCESS;
}
