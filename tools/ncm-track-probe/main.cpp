#include "NcmReader.h"

#include "aes.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QtEndian>

#include <array>
#include <cstring>

namespace {

constexpr quint32 kMaximumMetadataSize = 64 * 1024 * 1024;
constexpr std::array<unsigned char, 17> kModifyKey{
    0x23, 0x31, 0x34, 0x6c, 0x6a, 0x6b, 0x5f, 0x21, 0x5c,
    0x5d, 0x26, 0x30, 0x55, 0x3c, 0x27, 0x28, 0x00};
const QStringList kTrackKeys{
    QStringLiteral("trackNumber"),
    QStringLiteral("trackNum"),
    QStringLiteral("track"),
    QStringLiteral("track_no"),
    QStringLiteral("trackIndex"),
    QStringLiteral("track_index")};

bool readUInt32(QFile& file, quint32& value, QString& error)
{
    const QByteArray bytes = file.read(4);
    if (bytes.size() != 4) {
        error = QStringLiteral("Unexpected end of file reading a 32-bit length");
        return false;
    }

    value = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData()));
    return true;
}

QByteArray decryptMetadataBlock(const QByteArray& encrypted)
{
    if (encrypted.isEmpty() || encrypted.size() % 16 != 0) {
        return {};
    }

    AES aes(kModifyKey.data());
    QByteArray decrypted(encrypted.size(), '\0');
    for (int offset = 0; offset < encrypted.size(); offset += 16) {
        unsigned char input[16]{};
        unsigned char output[16]{};
        std::memcpy(input, encrypted.constData() + offset, sizeof(input));
        aes.decrypt(input, output);
        std::memcpy(decrypted.data() + offset, output, sizeof(output));
    }

    const auto padding =
        static_cast<unsigned char>(decrypted.constData()[decrypted.size() - 1]);
    if (padding > 0 && padding <= 16) {
        decrypted.chop(static_cast<int>(padding));
    }
    return decrypted;
}

bool readRawMetadata(const QString& path, QJsonObject& metadata,
                     QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.read(8) != QByteArrayLiteral("CTENFDAM") || !file.seek(10)) {
        error = QStringLiteral("Invalid NCM header");
        return false;
    }

    quint32 keyLength = 0;
    if (!readUInt32(file, keyLength, error)) {
        return false;
    }
    if (keyLength > kMaximumMetadataSize ||
        keyLength > static_cast<quint64>(file.size() - file.pos()) ||
        !file.seek(file.pos() + keyLength)) {
        error = QStringLiteral("Invalid NCM key section length");
        return false;
    }

    quint32 metadataLength = 0;
    if (!readUInt32(file, metadataLength, error)) {
        return false;
    }
    if (metadataLength == 0) {
        error = QStringLiteral("NCM has no metadata block");
        return false;
    }
    if (metadataLength > kMaximumMetadataSize ||
        metadataLength > static_cast<quint64>(file.size() - file.pos())) {
        error = QStringLiteral("Invalid NCM metadata section length");
        return false;
    }

    QByteArray encrypted = file.read(metadataLength);
    if (encrypted.size() != static_cast<qsizetype>(metadataLength)) {
        error = QStringLiteral("Truncated NCM metadata section");
        return false;
    }
    for (char& byte : encrypted) {
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x63);
    }
    if (encrypted.size() <= 22) {
        error = QStringLiteral("NCM metadata block is too short");
        return false;
    }

    const QByteArray decodedBase64 =
        QByteArray::fromBase64(encrypted.mid(22));
    QByteArray jsonBytes = decryptMetadataBlock(decodedBase64);
    if (jsonBytes.size() <= 6) {
        error = QStringLiteral("Unable to decrypt NCM metadata JSON");
        return false;
    }
    jsonBytes.remove(0, 6);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid decrypted JSON: %1")
                    .arg(parseError.errorString());
        return false;
    }

    metadata = document.object();
    return true;
}

int positiveInteger(const QJsonValue& value)
{
    if (value.isDouble()) {
        return value.toInt();
    }
    if (value.isString()) {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        return ok ? number : 0;
    }
    return 0;
}

int rawTrackNumber(const QJsonObject& object)
{
    for (const QString& key : kTrackKeys) {
        const int value = positiveInteger(object.value(key));
        if (value > 0) {
            return value;
        }
    }
    return 0;
}

QJsonObject recognizedTrackFields(const QJsonObject& object)
{
    QJsonObject fields;
    for (const QString& key : kTrackKeys) {
        if (object.contains(key)) {
            fields.insert(key, object.value(key));
        }
    }
    return fields;
}

QStringList collectNcmFiles(const QStringList& inputs, QStringList& errors)
{
    QStringList files;
    QSet<QString> seen;
    for (const QString& input : inputs) {
        const QFileInfo info(input);
        if (!info.exists()) {
            errors.push_back(QStringLiteral("Path does not exist: %1").arg(input));
            continue;
        }

        if (info.isDir()) {
            QDirIterator iterator(info.absoluteFilePath(), QDir::Files,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QString path = iterator.next();
                if (QFileInfo(path).suffix().compare(
                        QStringLiteral("ncm"), Qt::CaseInsensitive) != 0) {
                    continue;
                }
                const QString canonical = QFileInfo(path).canonicalFilePath();
                if (!canonical.isEmpty() && !seen.contains(canonical)) {
                    seen.insert(canonical);
                    files.push_back(path);
                }
            }
        } else if (info.suffix().compare(
                       QStringLiteral("ncm"), Qt::CaseInsensitive) == 0) {
            const QString canonical = info.canonicalFilePath();
            if (!canonical.isEmpty() && !seen.contains(canonical)) {
                seen.insert(canonical);
                files.push_back(info.absoluteFilePath());
            }
        } else {
            errors.push_back(QStringLiteral("Not an NCM file or directory: %1")
                                 .arg(input));
        }
    }

    files.sort(Qt::CaseInsensitive);
    return files;
}

QString diagnostic(const bool rawOk, const int rawNumber,
                   const int parsedNumber)
{
    if (!rawOk) {
        return QStringLiteral(
            "Raw NCM tags could not be decoded; inspect the error above.");
    }
    if (rawNumber > 0 && parsedNumber == 0) {
        return QStringLiteral(
            "A positive track number exists in raw JSON but NcmReader returned 0.");
    }
    if (rawNumber > 0 && parsedNumber != rawNumber) {
        return QStringLiteral(
            "Raw JSON and NcmReader disagree on the track number.");
    }
    if (rawNumber == 0 && parsedNumber > 0) {
        return QStringLiteral(
            "No positive track number in NCM JSON; NcmReader found one in the "
            "embedded audio metadata or filename.");
    }
    return QStringLiteral(
        "No positive track number in NCM JSON, embedded audio metadata, or "
        "filename.");
}

void printFileReport(const QString& path, QTextStream& out, bool& failed)
{
    QJsonObject rawMetadata;
    QString rawError;
    const bool rawOk = readRawMetadata(path, rawMetadata, rawError);

    QString readerError;
    const auto parsed =
        isac::format::ncm::NcmReader::inspect(path, &readerError);
    if (!parsed.valid) {
        failed = true;
    }

    const int rawNumber = rawOk ? rawTrackNumber(rawMetadata) : 0;
    out << "File: " << path << '\n';
    if (rawOk) {
        out << "Raw metadata JSON:\n"
            << QJsonDocument(rawMetadata).toJson(QJsonDocument::Indented)
            << "Recognized raw track fields: "
            << QJsonDocument(recognizedTrackFields(rawMetadata))
                   .toJson(QJsonDocument::Compact)
            << '\n';
    } else {
        out << "Raw metadata error: " << rawError << '\n';
    }

    out << "NcmReader: valid=" << (parsed.valid ? "true" : "false")
        << ", trackNumber=" << parsed.trackNumber
        << ", discNumber=" << parsed.discNumber
        << ", title=\"" << parsed.title
        << "\", artist=\"" << parsed.artist
        << "\", album=\"" << parsed.album << "\"\n";
    if (!parsed.valid && !readerError.isEmpty()) {
        out << "NcmReader error: " << readerError << '\n';
    }
    out << "Diagnosis: "
        << diagnostic(rawOk, rawNumber, parsed.trackNumber)
        << "\n\n";
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ncm-track-probe"));

    QTextStream out(stdout);
    QStringList inputs = app.arguments().mid(1);
    if (inputs.size() == 3 &&
        inputs.first() == QStringLiteral("--materialize")) {
        QString error;
        if (!isac::format::ncm::NcmReader::materialize(
                inputs.at(1), inputs.at(2), &error)) {
            QTextStream err(stderr);
            err << "Unable to materialize NCM audio: " << error << '\n';
            return 1;
        }
        out << "Decoded audio written to: "
            << QFileInfo(inputs.at(2)).absoluteFilePath() << '\n';
        return 0;
    }

    if (inputs.isEmpty() || inputs.contains(QStringLiteral("--help")) ||
        inputs.contains(QStringLiteral("-h"))) {
        out << "Usage:\n"
            << "  ncm-track-probe <file.ncm | directory> [...]\n"
            << "  ncm-track-probe --materialize <file.ncm> <output-audio-file>\n"
            << "Directories are searched recursively. Inspection reads NCM "
               "tags only; --materialize explicitly decodes the audio "
               "payload to the requested output path.\n";
        return inputs.isEmpty() ? 2 : 0;
    }

    QStringList pathErrors;
    const QStringList files = collectNcmFiles(inputs, pathErrors);
    for (const QString& error : pathErrors) {
        out << "Error: " << error << '\n';
    }
    if (files.isEmpty()) {
        if (pathErrors.isEmpty()) {
            out << "No .ncm files found.\n";
        }
        return 2;
    }

    bool failed = false;
    for (const QString& file : files) {
        printFileReport(file, out, failed);
    }

    out << "Scanned " << files.size() << " NCM file(s).\n";
    return failed ? 1 : 0;
}
