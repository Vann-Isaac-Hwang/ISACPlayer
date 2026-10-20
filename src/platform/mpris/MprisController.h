#pragma once

#include "library/LibraryTypes.h"
#include "library/PlaybackQueue.h"

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace isac::platform {

class MprisController final : public QObject {
    Q_OBJECT
public:
    explicit MprisController(QObject* parent = nullptr);
    ~MprisController() override;

    bool isAvailable() const noexcept { return m_registered; }
    void setTrack(const library::TrackInfo& track);
    void setPlaybackState(bool playing);
    void setPlaybackMode(library::PlaybackMode mode);
    void setVolume(double volume);
    void setPosition(double seconds);
    void notifySeeked(double seconds);
    QString trackObjectPath() const;

signals:
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 offsetMicroseconds);
    void setPositionRequested(const QString& trackPath, qint64 positionMicroseconds);
    void openUriRequested(const QString& uri);
    void volumeRequested(double volume);
    void playbackModeRequested(int mode);

private:
    friend class MprisRootAdaptor;
    friend class MprisPlayerAdaptor;
    void emitPropertiesChanged(const QString& interface,
                               const QVariantMap& changed,
                               const QStringList& invalidated = {});

    QString m_serviceName;
    bool m_registered = false;
    bool m_playing = false;
    bool m_shuffle = false;
    QString m_loopStatus = QStringLiteral("None");
    QVariantMap m_metadata;
    qint64 m_trackId = 0;
    qint64 m_positionMicroseconds = 0;
    double m_volume = 1.0;
};

class MprisRootAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)
public:
    explicit MprisRootAdaptor(MprisController* controller);

    QString identity() const { return QStringLiteral("ISACPlayer"); }
    QString desktopEntry() const { return QStringLiteral("isacplayer"); }
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes() const
    {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"),
                QStringLiteral("audio/wav")};
    }

public slots:
    void Raise();
    void Quit();

private:
    MprisController* m_controller;
};

class MprisPlayerAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qint64 Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious CONSTANT)
    Q_PROPERTY(bool CanPlay READ canPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ canPause CONSTANT)
    Q_PROPERTY(bool CanSeek READ canSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)
public:
    explicit MprisPlayerAdaptor(MprisController* controller);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString& status);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool enabled);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qint64 position() const;
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return true; }
    bool canGoPrevious() const { return true; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const;
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath& trackId, qlonglong position);
    void OpenUri(const QString& uri);

signals:
    void Seeked(qlonglong position);

private:
    MprisController* m_controller;
};

} // namespace isac::platform
