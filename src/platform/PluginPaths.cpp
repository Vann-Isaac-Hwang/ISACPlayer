#include "PluginPaths.h"

#include <QCoreApplication>
#include <QDir>

#ifndef ISACPLAYER_INSTALL_LIBDIR
#define ISACPLAYER_INSTALL_LIBDIR "lib"
#endif

namespace isac::platform {
namespace {

QString moduleDirectory(const QString& name)
{
    const QDir executableDirectory(QCoreApplication::applicationDirPath());
    const QString adjacent =
        executableDirectory.absoluteFilePath(name);
    const QString installed = executableDirectory.absoluteFilePath(
        QStringLiteral("../%1/isacplayer/%2")
            .arg(QStringLiteral(ISACPLAYER_INSTALL_LIBDIR), name));
    return resolvePluginDirectory({adjacent, installed});
}

} // namespace

QString resolvePluginDirectory(const QStringList& candidates)
{
    static const QStringList modulePatterns{
        QStringLiteral("*.so"),
        QStringLiteral("*.dll"),
        QStringLiteral("*.dylib")
    };

    for (const QString& candidate : candidates) {
        const QDir directory(candidate);
        if (directory.exists() &&
            !directory.entryInfoList(
                 modulePatterns, QDir::Files | QDir::Readable).isEmpty()) {
            return directory.absolutePath();
        }
    }

    return candidates.isEmpty()
        ? QString{}
        : QDir(candidates.front()).absolutePath();
}

QString dspPluginDirectory()
{
    return moduleDirectory(QStringLiteral("plugins"));
}

QString formatPluginDirectory()
{
    return moduleDirectory(QStringLiteral("format_plugins"));
}

} // namespace isac::platform
