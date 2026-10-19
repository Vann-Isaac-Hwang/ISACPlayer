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

class QDirIterator;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void playTrackAtIndex(int index);

    Ui::MainWindow *ui;
    isac::ui::PluginManager m_pluginManager;
    isac::AudioEngine m_audioEngine;

    QStackedWidget* m_stackedWidget = nullptr;
    isac::ui::AppleLibraryPage* m_libraryPage = nullptr;
    isac::ui::NowPlayingBar* m_nowPlayingBar = nullptr;
    isac::ui::PatchbayView* m_patchbayView = nullptr;
    QTimer* m_progressTimer = nullptr;
    QTimer* m_libraryScanTimer = nullptr;
    std::unique_ptr<QDirIterator> m_musicIterator;
    int m_currentTrackIndex = -1;
};
#endif // MAINWINDOW_H
