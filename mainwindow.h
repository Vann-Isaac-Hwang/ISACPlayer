#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>
#include <memory>
#include "engine/graph/AudioEngine.h"
#include "ui/apple_view/AppleMusicView.h"
#include "ui/patchbay/PatchbayView.h"
#include "ui/plugins/PluginManager.h"
#include "library/LibraryScanner.h"
#include "library/PlaybackQueue.h"
#ifdef ISAC_HAS_MPRIS
#include "platform/mpris/MprisController.h"
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void playTrackById(qint64 trackId);
    bool playQueuedTrack(qint64 trackId);
    void rebuildPlaybackQueue(qint64 currentTrackId);
    void persistPlaybackQueue(bool includeOrder);
    void restorePlaybackQueue();

    Ui::MainWindow *ui;
    isac::ui::PluginManager m_pluginManager;
    isac::library::LibraryRepository m_libraryRepository;
    isac::AudioEngine m_audioEngine;

    QStackedWidget* m_stackedWidget = nullptr;
    isac::ui::AppleLibraryPage* m_libraryPage = nullptr;
    isac::ui::NowPlayingBar* m_nowPlayingBar = nullptr;
    isac::ui::PatchbayView* m_patchbayView = nullptr;
    QTimer* m_progressTimer = nullptr;
    isac::library::LibraryScanner* m_libraryScanner = nullptr;
    isac::library::PlaybackQueue m_playbackQueue;
    isac::library::PlaybackMode m_playbackMode =
        isac::library::PlaybackMode::Sequential;
    bool m_currentTrackEndHandled = false;
    qint64 m_currentTrackId = 0;
    bool m_trackLoaded = false;
    bool m_playbackQueueNeedsSave = false;
#ifdef ISAC_HAS_MPRIS
    isac::platform::MprisController* m_mprisController = nullptr;
#endif
};
#endif // MAINWINDOW_H
