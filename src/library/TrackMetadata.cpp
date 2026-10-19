#include "TrackMetadata.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>

namespace isac::library {

TrackInfo MetadataReader::readTrack(const QString& filePath) {
    TrackInfo info;
    info.filePath = filePath;
    QFileInfo fi(filePath);
    info.title  = fi.completeBaseName();
    info.artist = "Unknown Artist";
    info.album  = "Unknown Album";

    TagLib::FileRef ref(filePath.toUtf8().constData());
    if (!ref.isNull() && ref.tag()) {
        TagLib::Tag* tag = ref.tag();
        if (!tag->title().isEmpty())  info.title  = QString::fromStdString(tag->title().to8Bit(true));
        if (!tag->artist().isEmpty()) info.artist = QString::fromStdString(tag->artist().to8Bit(true));
        if (!tag->album().isEmpty())  info.album  = QString::fromStdString(tag->album().to8Bit(true));
        if (ref.audioProperties())    info.durationSeconds = ref.audioProperties()->lengthInSeconds();
    }

    return info;
}

static QPixmap loadScaledPixmap(const QByteArray& encodedData, int size) {
    if (encodedData.isEmpty() || size <= 0) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(encodedData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    reader.setScaledSize(QSize(size, size));
    const QImage image = reader.read();
    return image.isNull() ? QPixmap{} : QPixmap::fromImage(image);
}

QPixmap MetadataReader::loadCoverArt(const TrackInfo& track, int size) {
    QPixmap cover;
    const QString& filePath = track.filePath;

    if (filePath.endsWith(".flac", Qt::CaseInsensitive)) {
        TagLib::FLAC::File flacFile(filePath.toUtf8().constData());
        if (flacFile.isValid()) {
            const auto& picList = flacFile.pictureList();
            if (!picList.isEmpty()) {
                const auto* pic = picList.front();
                const QByteArray data(
                    pic->data().data(), static_cast<int>(pic->data().size()));
                cover = loadScaledPixmap(data, size);
            }
        }
    }
    else if (filePath.endsWith(".mp3", Qt::CaseInsensitive)) {
        TagLib::MPEG::File mp3File(filePath.toUtf8().constData());
        if (mp3File.isValid() && mp3File.ID3v2Tag()) {
            auto frameList = mp3File.ID3v2Tag()->frameListMap()["APIC"];
            if (!frameList.isEmpty()) {
                auto* picFrame = static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frameList.front());
                const QByteArray data(
                    picFrame->picture().data(),
                    static_cast<int>(picFrame->picture().size()));
                cover = loadScaledPixmap(data, size);
            }
        }
    }

    if (cover.isNull()) {
        cover = generateDefaultCover(track.title, size);
    }
    return cover;
}

QPixmap MetadataReader::generateDefaultCover(const QString& title, int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // 根据标题哈希生成独特的 Apple Music 风格流体渐变色
    uint hash = qHash(title);
    QColor c1 = QColor::fromHsl((hash % 360), 175, 110);
    QColor c2 = QColor::fromHsl(((hash + 70) % 360), 195, 65);

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size, size), 18, 18);
    p.fillPath(path, grad);

    // 绘制居中音符图标
    p.setPen(QColor(255, 255, 255, 190));
    QFont f = p.font();
    f.setPixelSize(size / 3);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, "♫");

    return pix;
}

} // namespace isac::library
