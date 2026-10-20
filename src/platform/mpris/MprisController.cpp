#include "MprisController.h"
#include "library/TrackMetadata.h"

#include <QDir>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QCoreApplication>
#include <QPixmap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>
#include <QWidget>
#include <QApplication>
#include <algorithm>
#include <cmath>

namespace isac::platform {
namespace {

constexpr auto kRootPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";

}

MprisController::MprisController(QObject* parent)
    : QObject(parent)
    , m_serviceName(QStringLiteral("org.mpris.MediaPlayer2.isacplayer"))
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(m_serviceName)) {
        m_registered = bus.registerObject(
            QString::fromLatin1(kRootPath), this,
            QDBusConnection::ExportAdaptors);
        if (!m_registered) {
            bus.unregisterService(m_serviceName);
        }
    }
}

MprisController::~MprisController()
{
    if (m_registered) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QString::fromLatin1(kRootPath));
        bus.unregisterService(m_serviceName);
    }
}

void MprisController::setTrack(const library::TrackInfo& track)
{
    m_trackId = track.id;
    m_positionMicroseconds = 0;
    QString artUrl;
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cacheRoot.isEmpty()) {
        const QString artDirectory =
            QDir(cacheRoot).filePath(QStringLiteral("mpris-art"));
        if (QDir().mkpath(artDirectory)) {
            const QString artPath = QDir(artDirectory).filePath(
                QStringLiteral("track-%1.png").arg(track.id));
            const QPixmap cover = library::MetadataReader::loadCoverArt(track, 512);
            QSaveFile artFile(artPath);
            if (!cover.isNull() && artFile.open(QIODevice::WriteOnly) &&
                cover.save(&artFile, "PNG") && artFile.commit()) {
                artUrl = QUrl::fromLocalFile(artPath).toString();
            } else {
                artFile.cancelWriting();
            }
        }
    }
    m_metadata = {
        {QStringLiteral("mpris:trackid"),
         QVariant::fromValue(QDBusObjectPath(trackObjectPath()))},
        {QStringLiteral("mpris:length"),
         QVariant::fromValue<qlonglong>(
             static_cast<qlonglong>(track.durationSeconds) * 1000000)},
        {QStringLiteral("xesam:title"), track.title},
        {QStringLiteral("xesam:artist"), QStringList{track.artist}},
        {QStringLiteral("xesam:album"), track.album},
        {QStringLiteral("xesam:url"), QUrl::fromLocalFile(track.filePath).toString()}
    };
    if (!artUrl.isEmpty()) {
        m_metadata.insert(QStringLiteral("mpris:artUrl"), artUrl);
    }
    emitPropertiesChanged(
        QString::fromLatin1(kPlayerInterface),
        {{QStringLiteral("Metadata"), m_metadata},
         {QStringLiteral("Position"), QVariant::fromValue<qlonglong>(0)}});
}

void MprisController::setPlaybackState(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    emitPropertiesChanged(
        QString::fromLatin1(kPlayerInterface),
        {{QStringLiteral("PlaybackStatus"),
          playing ? QStringLiteral("Playing") : QStringLiteral("Paused")}});
}

void MprisController::setPlaybackMode(library::PlaybackMode mode)
{
    const bool shuffle = mode == library::PlaybackMode::Shuffle;
    const QString loopStatus = mode == library::PlaybackMode::RepeatOne
        ? QStringLiteral("Track")
        : mode == library::PlaybackMode::RepeatAll
            ? QStringLiteral("Playlist")
            : QStringLiteral("None");
    QVariantMap changed;
    if (shuffle != m_shuffle) {
        m_shuffle = shuffle;
        changed.insert(QStringLiteral("Shuffle"), shuffle);
    }
    if (loopStatus != m_loopStatus) {
        m_loopStatus = loopStatus;
        changed.insert(QStringLiteral("LoopStatus"), loopStatus);
    }
    if (!changed.isEmpty()) {
        emitPropertiesChanged(QString::fromLatin1(kPlayerInterface), changed);
    }
}

void MprisController::setVolume(double volume)
{
    if (!std::isfinite(volume)) {
        return;
    }
    const double bounded = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(m_volume, bounded)) {
        return;
    }
    m_volume = bounded;
    emit volumeRequested(bounded);
    emitPropertiesChanged(
        QString::fromLatin1(kPlayerInterface),
        {{QStringLiteral("Volume"), bounded}});
}

void MprisController::setPosition(double seconds)
{
    if (std::isfinite(seconds) && seconds >= 0.0) {
        m_positionMicroseconds =
            static_cast<qint64>(seconds * 1000000.0);
    }
}

void MprisController::notifySeeked(double seconds)
{
    setPosition(seconds);
    emitPropertiesChanged(
        QString::fromLatin1(kPlayerInterface),
        {{QStringLiteral("Position"),
          QVariant::fromValue<qlonglong>(m_positionMicroseconds)}});
    QDBusMessage message = QDBusMessage::createSignal(
        QString::fromLatin1(kRootPath),
        QString::fromLatin1(kPlayerInterface),
        QStringLiteral("Seeked"));
    message << QVariant::fromValue<qlonglong>(m_positionMicroseconds);
    QDBusConnection::sessionBus().send(message);
}

QString MprisController::trackObjectPath() const
{
    return m_trackId > 0
        ? QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(m_trackId)
        : QStringLiteral("/org/mpris/MediaPlayer2/Track/NoTrack");
}

void MprisController::emitPropertiesChanged(
    const QString& interface, const QVariantMap& changed,
    const QStringList& invalidated)
{
    if (!m_registered) {
        return;
    }
    QDBusMessage message = QDBusMessage::createSignal(
        QString::fromLatin1(kRootPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    message << interface << changed << invalidated;
    QDBusConnection::sessionBus().send(message);
}

MprisRootAdaptor::MprisRootAdaptor(MprisController* controller)
    : QDBusAbstractAdaptor(controller)
    , m_controller(controller)
{
}

void MprisRootAdaptor::Raise()
{
    if (auto* widget = qobject_cast<QWidget*>(m_controller->parent())) {
        widget->showNormal();
        widget->raise();
        widget->activateWindow();
    }
}

void MprisRootAdaptor::Quit()
{
    if (auto* application = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        application->quit();
    }
}

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisController* controller)
    : QDBusAbstractAdaptor(controller)
    , m_controller(controller)
{
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    return m_controller->m_playing
        ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

QString MprisPlayerAdaptor::loopStatus() const
{
    return m_controller->m_loopStatus;
}

void MprisPlayerAdaptor::setLoopStatus(const QString& status)
{
    if (status == QStringLiteral("Track")) {
        emit m_controller->playbackModeRequested(
            static_cast<int>(library::PlaybackMode::RepeatOne));
    } else if (status == QStringLiteral("Playlist")) {
        emit m_controller->playbackModeRequested(
            static_cast<int>(library::PlaybackMode::RepeatAll));
    } else if (status == QStringLiteral("None")) {
        emit m_controller->playbackModeRequested(
            static_cast<int>(m_controller->m_shuffle
                ? library::PlaybackMode::Shuffle
                : library::PlaybackMode::Sequential));
    }
}

bool MprisPlayerAdaptor::shuffle() const
{
    return m_controller->m_shuffle;
}

void MprisPlayerAdaptor::setShuffle(bool enabled)
{
    if (enabled) {
        emit m_controller->playbackModeRequested(
            static_cast<int>(library::PlaybackMode::Shuffle));
    } else {
        emit m_controller->playbackModeRequested(static_cast<int>(
            m_controller->m_loopStatus == QStringLiteral("Track")
                ? library::PlaybackMode::RepeatOne
                : m_controller->m_loopStatus == QStringLiteral("Playlist")
                    ? library::PlaybackMode::RepeatAll
                    : library::PlaybackMode::Sequential));
    }
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    return m_controller->m_metadata;
}

double MprisPlayerAdaptor::volume() const
{
    return m_controller->m_volume;
}

void MprisPlayerAdaptor::setVolume(double volume)
{
    m_controller->setVolume(volume);
}

qint64 MprisPlayerAdaptor::position() const
{
    return m_controller->m_positionMicroseconds;
}

bool MprisPlayerAdaptor::canSeek() const
{
    return m_controller->m_trackId > 0;
}

void MprisPlayerAdaptor::Next() { emit m_controller->nextRequested(); }
void MprisPlayerAdaptor::Previous() { emit m_controller->previousRequested(); }
void MprisPlayerAdaptor::Pause() { emit m_controller->pauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { emit m_controller->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit m_controller->stopRequested(); }
void MprisPlayerAdaptor::Play() { emit m_controller->playRequested(); }
void MprisPlayerAdaptor::Seek(qlonglong offset)
{
    emit m_controller->seekRequested(offset);
}

void MprisPlayerAdaptor::SetPosition(
    const QDBusObjectPath& trackId, qlonglong position)
{
    emit m_controller->setPositionRequested(
        trackId.path(), static_cast<qint64>(position));
}

void MprisPlayerAdaptor::OpenUri(const QString& uri)
{
    emit m_controller->openUriRequested(uri);
}

} // namespace isac::platform
