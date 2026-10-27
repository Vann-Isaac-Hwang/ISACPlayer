#pragma once

#include <isac/format/FormatPlugin.h>

#include <QObject>

class NcmFormatPlugin final : public QObject,
                              public isac::format::IAudioFormatPlugin,
                              public isac::format::IAudioFormatPluginCompatibility {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ISAC_FORMAT_PLUGIN_IID)
    Q_INTERFACES(isac::format::IAudioFormatPlugin
                 isac::format::IAudioFormatPluginCompatibility)

public:
    const char* pluginId() const noexcept override { return "isac.ncm"; }
    const char* displayName() const noexcept override { return "NCM Format"; }
    const char* description() const noexcept override {
        return "Reads NCM metadata and materializes audio on demand";
    }
    QStringList extensions() const override { return {QStringLiteral("ncm")}; }
    bool canHandle(const QString& filePath) const override;
    isac::format::FormatMetadata inspect(
        const QString& filePath) const override;
    bool materialize(const QString& sourcePath, const QString& outputPath,
                     QString* error = nullptr) const override;
    int pluginApiVersion() const noexcept override {
        return isac::format::FORMAT_PLUGIN_API_VERSION;
    }
};
