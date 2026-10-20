#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace isac::library {

struct TrackInfo {
    qint64 id = 0;
    QString filePath;
    QString title;
    QString artist;
    QString album;
    QString audioFormat;
    int durationSeconds = 0;
    int sampleRateHz = 0;
    int bitDepth = 0;
    int channels = 0;
    int bitrateKbps = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    qint64 fileSize = 0;
    QDateTime modifiedAt;
    QDateTime addedAt;
};

} // namespace isac::library
