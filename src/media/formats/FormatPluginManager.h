#pragma once

#include <isac/format/FormatPlugin.h>

#include <QPluginLoader>
#include <QString>
#include <memory>
#include <vector>

namespace isac::media {

class FormatPluginManager final {
public:
    explicit FormatPluginManager(const QString& pluginDirectory = {});

    void scanDirectory(const QString& directoryPath);
    isac::format::FormatMetadata inspect(const QString& filePath) const;
    bool materialize(const QString& filePath, const QString& outputPath,
                     QString* error = nullptr) const;
    QString pluginIdForFile(const QString& filePath) const;

private:
    struct Record {
        isac::format::IAudioFormatPlugin* plugin = nullptr;
        QString id;
    };

    const Record* recordForFile(const QString& filePath) const;

    std::vector<Record> m_records;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
};

} // namespace isac::media
