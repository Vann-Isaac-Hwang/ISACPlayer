#include "platform/PluginPaths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool writeModuleMarker(const QString& directory, const QString& name)
{
    if (!QDir().mkpath(directory)) {
        return false;
    }
    QFile marker(QDir(directory).filePath(name));
    return marker.open(QIODevice::WriteOnly);
}

void testInstalledPathWinsOverEmptyAdjacentDirectory()
{
    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "temporary directory must be valid");

    const QString adjacent =
        QDir(temporaryDirectory.path()).filePath("bin/plugins");
    const QString installed =
        QDir(temporaryDirectory.path()).filePath("lib/isacplayer/plugins");
    expect(QDir().mkpath(adjacent), "adjacent plugin directory must exist");
    expect(writeModuleMarker(installed, "libtest.so"),
           "installed module marker must be created");

    expect(isac::platform::resolvePluginDirectory({adjacent, installed}) ==
               QDir(installed).absolutePath(),
           "non-empty installed plugin directory must be selected");
}

void testAdjacentModulesRemainPreferred()
{
    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "temporary directory must be valid");

    const QString adjacent =
        QDir(temporaryDirectory.path()).filePath("build/plugins");
    const QString installed =
        QDir(temporaryDirectory.path()).filePath("lib/isacplayer/plugins");
    expect(writeModuleMarker(adjacent, "libdev.so"),
           "adjacent module marker must be created");
    expect(writeModuleMarker(installed, "libinstalled.so"),
           "installed module marker must be created");

    expect(isac::platform::resolvePluginDirectory({adjacent, installed}) ==
               QDir(adjacent).absolutePath(),
           "adjacent development modules must remain preferred");
}

void testEmptyCandidatesReturnAdjacentFallback()
{
    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "temporary directory must be valid");

    const QString adjacent =
        QDir(temporaryDirectory.path()).filePath("bin/format_plugins");
    const QString installed =
        QDir(temporaryDirectory.path()).filePath(
            "lib/isacplayer/format_plugins");

    expect(isac::platform::resolvePluginDirectory({adjacent, installed}) ==
               QDir(adjacent).absolutePath(),
           "empty plugin directories must return the adjacent fallback");
}

} // namespace

int main()
{
    testInstalledPathWinsOverEmptyAdjacentDirectory();
    testAdjacentModulesRemainPreferred();
    testEmptyCandidatesReturnAdjacentFallback();
    std::cout << "All plugin path tests passed.\n";
    return EXIT_SUCCESS;
}
