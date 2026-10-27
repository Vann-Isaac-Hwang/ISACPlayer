#include "FormatCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace isac::media {
namespace {

QString sourceKey(const QString& sourcePath, const QString& pluginId)
{
    const QFileInfo info(sourcePath);
    const QString material = QStringLiteral("%1|%2|%3|%4")
                                 .arg(QFileInfo(sourcePath).absoluteFilePath())
                                 .arg(info.size())
                                 .arg(info.lastModified().toUTC().toMSecsSinceEpoch())
                                 .arg(pluginId);
    return QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

} // namespace

QString FormatCache::rootDirectory()
{
    QString root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        root = QDir::tempPath() + QStringLiteral("/ISACPlayer");
    }
    QDir().mkpath(root);
    QDir().mkpath(QDir(root).filePath(QStringLiteral("format-cache")));
    return QDir(root).filePath(QStringLiteral("format-cache"));
}

QString FormatCache::playbackPath(const QString& sourcePath,
                                  const QString& pluginId,
                                  const QString& extension)
{
    const QString directory = QDir(rootDirectory()).filePath(
        QStringLiteral("playback/%1").arg(pluginId));
    QDir().mkpath(directory);
    return QDir(directory).filePath(
        sourceKey(sourcePath, pluginId) + QStringLiteral(".") +
        extension.toLower().remove(QLatin1Char('.')));
}

QString FormatCache::coverPath(const QString& sourcePath,
                               const QString& pluginId,
                               const QByteArray& coverData)
{
    if (coverData.isEmpty()) {
        return {};
    }

    const QString directory = QDir(rootDirectory()).filePath(
        QStringLiteral("covers/%1").arg(pluginId));
    QDir().mkpath(directory);
    const QByteArray hashInput = coverData +
        sourceKey(sourcePath, pluginId).toUtf8();
    const QByteArray hash = QCryptographicHash::hash(
        hashInput, QCryptographicHash::Sha256).toHex();
    const QString extension = coverData.startsWith(
        QByteArray::fromHex("89504e470d0a1a0a"))
        ? QStringLiteral("png")
        : QStringLiteral("jpg");
    return QDir(directory).filePath(
        QString::fromLatin1(hash) + QStringLiteral(".") + extension);
}

bool FormatCache::storeBytes(const QString& path, const QByteArray& data,
                             QString* error)
{
    if (data.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The format plugin returned empty data");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(data) != data.size() ||
        !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace isac::media
