#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include "engine/graph/AudioEngine.h"
#include "ui/isac_music/ISACMusicView.h"
#include "ui/isac_music/PlaybackQueueView.h"
#include "ui/patchbay/PatchbayView.h"
#include "ui/plugins/PluginManager.h"
#include "ui/theme/ThemeManager.h"
#include "library/LibraryScanner.h"
#include "library/PlaybackQueue.h"
#include "media/formats/FormatCache.h"
#include "media/formats/FormatPluginManager.h"
#include "media/formats/FormatPlaybackPrefetcher.h"
#ifdef ISAC_HAS_MPRIS
#include "platform/mpris/MprisController.h"
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QDockWidget;
class QResizeEvent;
class QShowEvent;
namespace isac::ui { class SmoothScrollFilter; }

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateBlurRegion();
    void updateSidebarAppearance();
    void updateInfoBarAppearance();
    void updatePlaybackSurfacesAppearance();
    void updateMainPageBackground();
    void setRouterDockWidgetsVisible(bool visible);
    void playTrackById(qint64 trackId);
    void queueTrackNext(qint64 trackId);
    bool playQueuedTrack(qint64 trackId);
    void rebuildPlaybackQueue(qint64 currentTrackId);
    void persistPlaybackQueue(bool includeOrder);
    void restorePlaybackQueue();
    void setPlaybackMode(isac::library::PlaybackMode mode);
    void refreshPlaybackQueueView(bool force = false);
    std::vector<qint64> currentTrackIds() const;
    void prefetchUpcomingFormatTracks();
    QString resolvePlaybackPath(const isac::library::TrackInfo& track,
                                QString* error = nullptr);

    isac::ui::ThemeManager m_themeManager;
    isac::ui::SmoothScrollFilter* m_smoothScrollFilter = nullptr;
    Ui::MainWindow *ui;
    isac::ui::PluginManager m_pluginManager;
    isac::media::FormatPluginManager m_formatPluginManager;
    isac::media::FormatPlaybackPrefetcher m_formatPlaybackPrefetcher;
    isac::library::LibraryRepository m_libraryRepository;
    isac::AudioEngine m_audioEngine;

    QWidget* m_navigationSidebar = nullptr;
    bool m_sidebarTransparencySupported = false;
    bool m_sidebarBlurSupported = false;
    bool m_sidebarTransparencyEnabled = false;
    bool m_sidebarBlurEnabled = false;
    int m_sidebarOpacityPercent = 78;
    int m_sidebarFontSize = 13;
    bool m_infoBarTransparencyEnabled = false;
    bool m_infoBarBlurEnabled = false;
    int m_infoBarOpacityPercent = 78;
    bool m_playbackControlsTransparencyEnabled = false;
    bool m_playbackControlsBlurEnabled = false;
    int m_playbackControlsOpacityPercent = 78;
    bool m_progressBarTransparencyEnabled = false;
    bool m_progressBarBlurEnabled = false;
    int m_progressBarOpacityPercent = 78;
    QStackedWidget* m_stackedWidget = nullptr;
    QPushButton* m_navLibraryButton = nullptr;
    isac::ui::ISACMusicLibraryPage* m_libraryPage = nullptr;
    isac::ui::PlaylistPage* m_playlistPage = nullptr;
    isac::ui::NowPlayingHeader* m_nowPlayingHeader = nullptr;
    QWidget* m_playbackInfoBar = nullptr;
    QWidget* m_playbackControlBlurRegion = nullptr;
    QWidget* m_playbackVolumeBlurRegion = nullptr;
    QWidget* m_progressBarBlurRegion = nullptr;
    isac::ui::PlaybackQueueView* m_playbackQueueView = nullptr;
    isac::ui::PatchbayView* m_patchbayView = nullptr;
    QMainWindow* m_routerWorkspace = nullptr;
    QDockWidget* m_pluginDock = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    QTimer* m_progressTimer = nullptr;
    QTimer* m_visualizationTimer = nullptr;
    std::array<double, 16384> m_preVisualizationLeft{};
    std::array<double, 16384> m_preVisualizationRight{};
    std::array<double, 16384> m_postVisualizationLeft{};
    std::array<double, 16384> m_postVisualizationRight{};
    std::array<std::uint64_t, 3> m_lastAudioDiagnosticCounts{};
    std::chrono::steady_clock::time_point m_lastAudioDiagnosticLog{};
    isac::library::LibraryScanner* m_libraryScanner = nullptr;
    isac::library::PlaybackQueue m_playbackQueue;
    isac::library::PlaybackMode m_playbackMode =
        isac::library::PlaybackMode::Sequential;
    bool m_currentTrackEndHandled = false;
    qint64 m_currentTrackId = 0;
    bool m_trackLoaded = false;
    bool m_playbackQueueNeedsSave = false;
    qint64 m_preparedNextTrackId = 0;
    QString m_lastPlaybackError;
#ifdef ISAC_HAS_MPRIS
    isac::platform::MprisController* m_mprisController = nullptr;
#endif
};
#endif // MAINWINDOW_H
