#pragma once

#include <QByteArray>
#include <QString>

namespace isac::media {

class FormatCache final {
public:
    static QString rootDirectory();
    static QString playbackPath(const QString& sourcePath,
                                const QString& pluginId,
                                const QString& extension);
    static QString coverPath(const QString& sourcePath,
                             const QString& pluginId,
                             const QByteArray& coverData);
    static bool storeBytes(const QString& path, const QByteArray& data,
                           QString* error = nullptr);
};

} // namespace isac::media
