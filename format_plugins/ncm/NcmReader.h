#pragma once

#include <isac/format/FormatPlugin.h>

#include <QString>

namespace isac::format::ncm {

class NcmReader final {
public:
    static FormatMetadata inspect(const QString& filePath,
                                  QString* error = nullptr);
    static bool materialize(const QString& sourcePath,
                            const QString& outputPath,
                            QString* error = nullptr);
};

} // namespace isac::format::ncm
