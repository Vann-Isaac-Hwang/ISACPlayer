#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtPlugin>

namespace isac::format {

constexpr int FORMAT_PLUGIN_API_VERSION = 1;
#define ISAC_FORMAT_PLUGIN_IID "org.isacplayer.FormatPlugin/1.0"
#define ISAC_FORMAT_PLUGIN_COMPATIBILITY_IID \
    "org.isacplayer.FormatPluginCompatibility/1.0"

struct FormatMetadata {
    bool valid = false;
    QString title;
    QString artist;
    QString album;
    QString decodedFormat;
    int durationMilliseconds = 0;
    int bitrateKbps = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    QByteArray coverData;
};

class IAudioFormatPlugin {
public:
    virtual ~IAudioFormatPlugin() = default;
    virtual const char* pluginId() const noexcept = 0;
    virtual const char* displayName() const noexcept = 0;
    virtual const char* description() const noexcept = 0;
    virtual QStringList extensions() const = 0;
    virtual bool canHandle(const QString& filePath) const = 0;
    virtual FormatMetadata inspect(const QString& filePath) const = 0;
    virtual bool materialize(const QString& sourcePath,
                             const QString& outputPath,
                             QString* error = nullptr) const = 0;
};

class IAudioFormatPluginCompatibility {
public:
    virtual ~IAudioFormatPluginCompatibility() = default;
    virtual int pluginApiVersion() const noexcept = 0;
};

} // namespace isac::format

Q_DECLARE_INTERFACE(isac::format::IAudioFormatPlugin,
                    ISAC_FORMAT_PLUGIN_IID)
Q_DECLARE_INTERFACE(isac::format::IAudioFormatPluginCompatibility,
                    ISAC_FORMAT_PLUGIN_COMPATIBILITY_IID)
