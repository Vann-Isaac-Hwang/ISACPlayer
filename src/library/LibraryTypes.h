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
    int durationSeconds = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    qint64 fileSize = 0;
    QDateTime modifiedAt;
    QDateTime addedAt;
};

} // namespace isac::library
