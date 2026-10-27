#include "NcmFormatPlugin.h"

#include "NcmReader.h"

#include <QFileInfo>

bool NcmFormatPlugin::canHandle(const QString& filePath) const
{
    return QFileInfo(filePath).suffix().compare(
        QStringLiteral("ncm"), Qt::CaseInsensitive) == 0;
}

isac::format::FormatMetadata
NcmFormatPlugin::inspect(const QString& filePath) const
{
    return isac::format::ncm::NcmReader::inspect(filePath);
}

bool NcmFormatPlugin::materialize(const QString& sourcePath,
                                  const QString& outputPath,
                                  QString* error) const
{
    return isac::format::ncm::NcmReader::materialize(
        sourcePath, outputPath, error);
}
