#pragma once
#include "LibraryTypes.h"
#include <QPixmap>

namespace isac::library {

class MetadataReader {
public:
    static TrackInfo readTrack(const QString& filePath);
    // Cover art is decoded only when a track is shown.
    static QPixmap loadCoverArt(const TrackInfo& track, int size = 256);
    static QPixmap generateDefaultCover(const QString& title, int size = 256);
};

} // namespace isac::library
