#include "FormatPluginManager.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(formatPluginLog, "isac.format_plugins")
}

namespace isac::media {

FormatPluginManager::FormatPluginManager(const QString& pluginDirectory)
{
    if (!pluginDirectory.isEmpty()) {
        scanDirectory(pluginDirectory);
    }
}

void FormatPluginManager::scanDirectory(const QString& directoryPath)
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        return;
    }

    const QStringList patterns{"*.so", "*.dll", "*.dylib"};
    for (const QFileInfo& file : directory.entryInfoList(
             patterns, QDir::Files | QDir::Readable, QDir::Name)) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject* instance = loader->instance();
        auto* plugin = qobject_cast<isac::format::IAudioFormatPlugin*>(instance);
        auto* compatibility =
            qobject_cast<isac::format::IAudioFormatPluginCompatibility*>(
                instance);
        if (!plugin || !compatibility ||
            compatibility->pluginApiVersion() !=
                isac::format::FORMAT_PLUGIN_API_VERSION) {
            qCWarning(formatPluginLog)
                << "Ignoring invalid or incompatible format plugin:"
                << file.filePath() << loader->errorString();
            continue;
        }

        m_records.push_back({plugin, QString::fromUtf8(plugin->pluginId())});
        m_loaders.push_back(std::move(loader));
    }
}

const FormatPluginManager::Record*
FormatPluginManager::recordForFile(const QString& filePath) const
{
    for (const auto& record : m_records) {
        if (record.plugin && record.plugin->canHandle(filePath)) {
            return &record;
        }
    }
    return nullptr;
}

isac::format::FormatMetadata
FormatPluginManager::inspect(const QString& filePath) const
{
    const auto* record = recordForFile(filePath);
    return record && record->plugin
        ? record->plugin->inspect(filePath)
        : isac::format::FormatMetadata{};
}

bool FormatPluginManager::materialize(const QString& filePath,
                                      const QString& outputPath,
                                      QString* error) const
{
    const auto* record = recordForFile(filePath);
    if (!record || !record->plugin) {
        if (error) {
            *error = QStringLiteral("No format plugin handles this file");
        }
        return false;
    }
    return record->plugin->materialize(filePath, outputPath, error);
}

QString FormatPluginManager::pluginIdForFile(const QString& filePath) const
{
    const auto* record = recordForFile(filePath);
    return record ? record->id : QString{};
}

} // namespace isac::media
