#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "ui/inspector/NodeInspectorWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDockWidget>
#include <QListWidget>
#include <QCoreApplication>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

namespace {

QString libraryDatabasePath()
{
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDirectory);
    return QDir(dataDirectory).filePath(QStringLiteral("library.sqlite"));
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(QCoreApplication::applicationDirPath() + "/plugins")
    , m_libraryRepository(libraryDatabasePath())
    , m_audioEngine(
          m_pluginManager.createNode("isac.gain"),
          m_pluginManager.createNode("isac.add"),
          m_pluginManager.createNode("isac.biquad"))
{
    ui->setupUi(this);
    setWindowTitle("ISACPlayer — Apple Music & DSP Patchbay Workstation");
    resize(1180, 760);

    // 全局暗黑主题背景
    ui->centralwidget->setStyleSheet("background-color: #14161C;");
    ui->menubar->setVisible(false);
    ui->statusbar->setVisible(false);

    auto* rootLayout = new QVBoxLayout(ui->centralwidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* upperLayout = new QHBoxLayout();
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(0);

    // ==================== 1. 左侧 Apple Music 风格侧边导航栏 ====================
    auto* sidebar = new QWidget(this);
    sidebar->setFixedWidth(220);
    sidebar->setStyleSheet(R"(
        QWidget { background-color: #191B22; border-right: 1px solid #262934; }
        QLabel { color: #6E7382; font-size: 11px; font-weight: bold; border: none; padding-left: 12px; }
        QPushButton {
            text-align: left; padding: 11px 16px; border: none; border-radius: 8px;
            color: #D0D4E0; font-size: 14px; font-weight: 500; background: transparent;
        }
        QPushButton:hover { background-color: #252834; color: white; }
        QPushButton:checked { background-color: #FA2D48; color: white; font-weight: bold; }
    )");

    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14, 24, 14, 20);
    sideLayout->setSpacing(8);

    auto* appLogo = new QLabel(" ISAC Music", sidebar);
    appLogo->setStyleSheet("color: white; font-size: 20px; font-weight: bold; padding-bottom: 14px;");

    auto* btnNavLibrary  = new QPushButton("🎵  音乐资料库", sidebar);
    auto* btnNavPatchbay = new QPushButton("🎛️  DSP 节点实验室", sidebar);
    btnNavLibrary->setCheckable(true);
    btnNavPatchbay->setCheckable(true);
    btnNavLibrary->setChecked(true);

    sideLayout->addWidget(appLogo);
    sideLayout->addWidget(new QLabel("APPLE MUSIC 视图", sidebar));
    sideLayout->addWidget(btnNavLibrary);
    sideLayout->addSpacing(16);
    sideLayout->addWidget(new QLabel("ENGINEERING 工程视图", sidebar));
    sideLayout->addWidget(btnNavPatchbay);
    sideLayout->addStretch();

    // ==================== 2. 中央多页面切换容器 (QStackedWidget) ====================
    m_stackedWidget = new QStackedWidget(this);

    // 页面 0：Apple Music 曲库界面
    m_libraryPage = new isac::ui::AppleLibraryPage(&m_libraryRepository, this);
    m_libraryPage->refreshTracks();
    m_libraryScanner = new isac::library::LibraryScanner(
        libraryDatabasePath(), this);
    connect(m_libraryScanner, &isac::library::LibraryScanner::batchCommitted,
            this, [this](int) { m_libraryPage->refreshTracks(); });
    connect(m_libraryScanner, &isac::library::LibraryScanner::progressChanged,
            this, [this](int visited) {
                m_libraryPage->setScanStatus(QString("已检查 %1 首").arg(visited));
            });
    connect(m_libraryScanner, &isac::library::LibraryScanner::scanFinished,
            this, [this](bool cancelled, const QString& error) {
                if (!error.isEmpty()) {
                    m_libraryPage->setScanStatus("扫描失败: " + error);
                } else if (cancelled) {
                    m_libraryPage->setScanStatus("扫描已取消");
                } else {
                    m_libraryPage->setScanStatus("扫描完成");
                }
                m_libraryPage->refreshTracks();
            });

    // 页面 1：Coppwr 风格 DSP 连线调音台页面
    auto* patchbayContainer = new QWidget(this);
    auto* patchLayout = new QVBoxLayout(patchbayContainer);
    patchLayout->setContentsMargins(24, 20, 24, 16);
    patchLayout->setSpacing(12);

    auto* patchHeader = new QHBoxLayout();
    auto* patchTitle = new QLabel("DSP 音频流拓扑实验室 (Coppwr Patchbay)", this);
    patchTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: white;");
    auto* lblPatchStatus = new QLabel("提示：左键从橙色针脚拉线至蓝色针脚，右键点击连线拆除", this);
    lblPatchStatus->setStyleSheet("color: #9499A6; font-size: 13px;");
    patchHeader->addWidget(patchTitle);
    patchHeader->addStretch();
    patchHeader->addWidget(lblPatchStatus);

    m_patchbayView = new isac::ui::PatchbayView(m_audioEngine, this);
    m_patchbayView->setStyleSheet("border: 1px solid #2B2F3C; border-radius: 12px;");

    // ==================== 3. 插件实例化 Dock ====================
    auto* pluginDock = new QDockWidget("DSP 模块库", this);
    pluginDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    pluginDock->setMinimumWidth(270);
    pluginDock->setStyleSheet(R"(
        QDockWidget { color: #E5E7EF; font-weight: bold; }
        QDockWidget::title { background: #191B22; padding: 9px; }
        QWidget { background: #191B22; color: #D0D4E0; }
        QListWidget {
            background: #14161C; border: 1px solid #2B2F3C;
            outline: none; padding: 4px;
        }
        QListWidget::item { padding: 9px 7px; border-radius: 5px; }
        QListWidget::item:selected { background: #343A4B; color: white; }
        QPushButton {
            background: #252834; color: #E0E4F0; border: 1px solid #343846;
            border-radius: 5px; padding: 8px; font-weight: bold;
        }
        QPushButton:hover { background: #FA2D48; border-color: #FA2D48; color: white; }
    )");

    auto* pluginPanel = new QWidget(pluginDock);
    auto* pluginLayout = new QVBoxLayout(pluginPanel);
    pluginLayout->setContentsMargins(10, 10, 10, 10);
    pluginLayout->setSpacing(8);

    auto* pluginList = new QListWidget(pluginPanel);
    auto* pluginDescription = new QLabel(pluginPanel);
    pluginDescription->setWordWrap(true);
    pluginDescription->setMinimumHeight(48);
    pluginDescription->setStyleSheet("color: #9499A6; padding: 3px;");
    auto* instantiateButton = new QPushButton("实例化到画布", pluginPanel);

    for (const auto& plugin : m_pluginManager.plugins()) {
        auto* item = new QListWidgetItem(plugin.displayName, pluginList);
        item->setData(Qt::UserRole, plugin.id);
        item->setToolTip(plugin.description);
    }
    if (pluginList->count() > 0) {
        pluginList->setCurrentRow(0);
    }

    auto updatePluginDescription = [pluginList, pluginDescription]() {
        const auto* item = pluginList->currentItem();
        pluginDescription->setText(item ? item->toolTip() : "请选择一个 DSP 模块");
    };
    updatePluginDescription();

    auto instantiateSelected = [this, pluginList, lblPatchStatus]() {
        const auto* item = pluginList->currentItem();
        if (!item) {
            return;
        }

        const auto node = m_pluginManager.createNode(item->data(Qt::UserRole).toString());
        if (!node) {
            lblPatchStatus->setText("插件实例化失败");
            return;
        }

        const isac::NodeId nodeId = m_audioEngine.addNode(node);
        if (!m_audioEngine.rebuildGraph()) {
            lblPatchStatus->setText("插件已创建，但图编译失败");
            return;
        }

        m_patchbayView->patchbayScene()->syncFromEngine();
        lblPatchStatus->setText(QString("已实例化: %1 (Node %2)")
                                    .arg(item->text())
                                    .arg(nodeId));
    };

    connect(pluginList, &QListWidget::currentItemChanged,
            this, [updatePluginDescription](QListWidgetItem*, QListWidgetItem*) {
                updatePluginDescription();
            });
    connect(instantiateButton, &QPushButton::clicked, this, instantiateSelected);
    connect(pluginList, &QListWidget::itemDoubleClicked,
            this, [instantiateSelected](QListWidgetItem*) { instantiateSelected(); });

    pluginLayout->addWidget(pluginList, 1);
    pluginLayout->addWidget(pluginDescription);
    pluginLayout->addWidget(instantiateButton);
    pluginDock->setWidget(pluginPanel);
    addDockWidget(Qt::RightDockWidgetArea, pluginDock);

    auto* inspectorDock = new QDockWidget("节点检视器", this);
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    inspectorDock->setMinimumWidth(320);
    auto* inspector = new isac::ui::NodeInspectorWidget(inspectorDock);
    inspectorDock->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    splitDockWidget(pluginDock, inspectorDock, Qt::Vertical);
    pluginDock->hide();
    inspectorDock->hide();

    connect(m_patchbayView->patchbayScene(), &QGraphicsScene::selectionChanged,
            this, [this, inspector]() {
                for (auto* item : m_patchbayView->patchbayScene()->selectedItems()) {
                    if (auto* nodeItem = dynamic_cast<isac::ui::VisualNodeItem*>(item)) {
                        const auto node = nodeItem->audioNode();
                        inspector->setNode(
                            node,
                            m_pluginManager.createControlWidget(node, inspector));
                        return;
                    }
                }
                inspector->clearNode();
            });

    auto* presetRow = new QHBoxLayout();
    QString presetBtnStyle = R"(
        QPushButton {
            background-color: #252834; color: #E0E4F0; border: 1px solid #343846;
            border-radius: 6px; padding: 7px 14px; font-weight: bold;
        }
        QPushButton:hover { background-color: #FA2D48; border-color: #FA2D48; color: white; }
    )";
    auto* btnStereo    = new QPushButton("预设1: 标准立体声", this);
    auto* btnSwapLR    = new QPushButton("预设2: 左右声道反接", this);
    auto* btnMonoRight = new QPushButton("预设3: 加法器混音 -> 仅右耳", this);
    auto* btnClear     = new QPushButton("清空所有连线", this);
    btnStereo->setStyleSheet(presetBtnStyle);
    btnSwapLR->setStyleSheet(presetBtnStyle);
    btnMonoRight->setStyleSheet(presetBtnStyle);
    btnClear->setStyleSheet(presetBtnStyle);
    presetRow->addWidget(btnStereo);
    presetRow->addWidget(btnSwapLR);
    presetRow->addWidget(btnMonoRight);
    presetRow->addWidget(btnClear);
    presetRow->addStretch();

    patchLayout->addLayout(patchHeader);
    patchLayout->addWidget(m_patchbayView, 1);
    patchLayout->addLayout(presetRow);

    m_stackedWidget->addWidget(m_libraryPage);
    m_stackedWidget->addWidget(patchbayContainer);

    upperLayout->addWidget(sidebar);
    upperLayout->addWidget(m_stackedWidget, 1);

    // ==================== 3. 底部常驻播放控制条 ====================
    m_nowPlayingBar = new isac::ui::NowPlayingBar(this);

    rootLayout->addLayout(upperLayout, 1);
    rootLayout->addWidget(m_nowPlayingBar);

    // ==================== 信号与槽绑定 ====================
    connect(btnNavLibrary, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPatchbay, pluginDock, inspectorDock]() {
        btnNavLibrary->setChecked(true);
        btnNavPatchbay->setChecked(false);
        m_stackedWidget->setCurrentIndex(0);
        pluginDock->hide();
        inspectorDock->hide();
    });
    connect(btnNavPatchbay, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPatchbay, pluginDock, inspectorDock]() {
        btnNavLibrary->setChecked(false);
        btnNavPatchbay->setChecked(true);
        m_stackedWidget->setCurrentIndex(1);
        pluginDock->show();
        inspectorDock->show();
    });

#ifdef ISAC_HAS_MPRIS
    m_mprisController = new isac::platform::MprisController(this);
    m_mprisController->setPlaybackMode(m_playbackMode);
    if (!m_mprisController->isAvailable()) {
        qWarning() << "Unable to register the KDE MPRIS D-Bus service";
    }
    connect(m_mprisController, &isac::platform::MprisController::playRequested,
            this, [this]() {
                if (m_currentTrackId > 0) {
                    m_audioEngine.setPlaying(true);
                    m_nowPlayingBar->setPlayingState(m_audioEngine.isPlaying());
                    m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
                    return;
                }
                const auto ids = m_libraryPage->trackIds();
                if (!ids.empty()) {
                    playTrackById(ids.front());
                }
            });
    connect(m_mprisController, &isac::platform::MprisController::pauseRequested,
            this, [this]() {
                m_audioEngine.setPlaying(false);
                m_nowPlayingBar->setPlayingState(false);
                m_mprisController->setPlaybackState(false);
            });
    connect(m_mprisController, &isac::platform::MprisController::playPauseRequested,
            this, [this]() {
                if (m_currentTrackId <= 0) {
                    const auto ids = m_libraryPage->trackIds();
                    if (!ids.empty()) {
                        playTrackById(ids.front());
                    }
                    return;
                }
                const bool playing = !m_audioEngine.isPlaying();
                m_audioEngine.setPlaying(playing);
                m_nowPlayingBar->setPlayingState(m_audioEngine.isPlaying());
                m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
            });
    connect(m_mprisController, &isac::platform::MprisController::stopRequested,
            this, [this]() {
                m_audioEngine.setPlaying(false);
                m_audioEngine.seekToSeconds(0.0);
                m_nowPlayingBar->setPlayingState(false);
                m_nowPlayingBar->updateProgress(
                    0.0, m_audioEngine.totalDurationSeconds());
                m_mprisController->setPosition(0.0);
                m_mprisController->setPlaybackState(false);
            });
    connect(m_mprisController, &isac::platform::MprisController::nextRequested,
            this, [this]() {
                const auto next = m_playbackQueue.next();
                if (next) {
                    playQueuedTrack(*next);
                }
            });
    connect(m_mprisController, &isac::platform::MprisController::previousRequested,
            this, [this]() {
                const auto previous = m_playbackQueue.previous();
                if (previous) {
                    playQueuedTrack(*previous);
                }
            });
    connect(m_mprisController, &isac::platform::MprisController::seekRequested,
            this, [this](qint64 offsetMicroseconds) {
                const double target = m_audioEngine.currentPositionSeconds() +
                    static_cast<double>(offsetMicroseconds) / 1000000.0;
                m_audioEngine.seekToSeconds(target);
                m_mprisController->notifySeeked(
                    m_audioEngine.currentPositionSeconds());
            });
    connect(m_mprisController,
            &isac::platform::MprisController::setPositionRequested,
            this, [this](const QString& trackPath, qint64 positionMicroseconds) {
                if (trackPath != m_mprisController->trackObjectPath()) {
                    return;
                }
                m_audioEngine.seekToSeconds(
                    static_cast<double>(positionMicroseconds) / 1000000.0);
                m_mprisController->notifySeeked(
                    m_audioEngine.currentPositionSeconds());
            });
    connect(m_mprisController, &isac::platform::MprisController::openUriRequested,
            this, [this](const QString& uri) {
                const QUrl url(uri);
                if (!url.isLocalFile()) {
                    return;
                }
                const auto track =
                    m_libraryRepository.trackByPath(url.toLocalFile());
                if (track) {
                    playTrackById(track->id);
                }
            });
    connect(m_mprisController, &isac::platform::MprisController::volumeRequested,
            this, [this](double volume) {
                m_nowPlayingBar->setVolume(volume);
                if (const auto gain = m_audioEngine.gainNode()) {
                    gain->setParameter("factor", static_cast<float>(volume));
                }
            });
    connect(m_mprisController,
            &isac::platform::MprisController::playbackModeRequested,
            this, [this](int mode) {
                m_playbackMode =
                    static_cast<isac::library::PlaybackMode>(mode);
                m_playbackQueue.setMode(m_playbackMode);
                m_nowPlayingBar->setPlaybackMode(mode);
                m_mprisController->setPlaybackMode(m_playbackMode);
            });
#endif

    // 导入单首或多首音乐
    connect(m_libraryPage, &isac::ui::AppleLibraryPage::requestImportFiles, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "选择音频文件", "", "Audio Files (*.mp3 *.flac *.wav)");
        if (!files.isEmpty()) {
            if (m_libraryScanner->isRunning()) {
                m_libraryPage->setScanStatus("已有扫描任务正在运行");
                return;
            }
            m_libraryPage->setScanStatus("正在读取音频元数据...");
            m_libraryScanner->scanFiles(files);
        }
    });

    // 递归扫描整个音乐文件夹
    connect(m_libraryPage, &isac::ui::AppleLibraryPage::requestImportFolder, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择音乐目录");
        if (!dir.isEmpty()) {
            if (m_libraryScanner->isRunning()) {
                m_libraryPage->setScanStatus("已有扫描任务正在运行");
                return;
            }
            m_libraryPage->setScanStatus("正在扫描文件夹...");
            m_libraryScanner->scanDirectory(dir);
        }
    });

    // 双击表格歌曲播放
    connect(m_libraryPage, &isac::ui::AppleLibraryPage::trackDoubleClicked,
            this, &MainWindow::playTrackById);

    // 底部控制栏事件
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::playPauseClicked, this, [this]() {
        bool nextState = !m_audioEngine.isPlaying();
        m_audioEngine.setPlaying(nextState);
        m_nowPlayingBar->setPlayingState(m_audioEngine.isPlaying());
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
#endif
    });
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::prevClicked, this, [this]() {
        const auto previous = m_playbackQueue.previous();
        if (previous) {
            playQueuedTrack(*previous);
        }
    });
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::nextClicked, this, [this]() {
        const auto next = m_playbackQueue.next();
        if (next) {
            playQueuedTrack(*next);
        }
    });
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::playbackModeChanged,
            this, [this](int mode) {
                m_playbackMode = static_cast<isac::library::PlaybackMode>(mode);
                m_playbackQueue.setMode(m_playbackMode);
#ifdef ISAC_HAS_MPRIS
                m_mprisController->setPlaybackMode(m_playbackMode);
#endif
            });
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::seekRequested, this, [this](double sec) {
        m_audioEngine.seekToSeconds(sec);
#ifdef ISAC_HAS_MPRIS
        m_mprisController->notifySeeked(m_audioEngine.currentPositionSeconds());
#endif
    });
    connect(m_nowPlayingBar, &isac::ui::NowPlayingBar::volumeChanged, this, [this](float factor) {
        if (const auto gain = m_audioEngine.gainNode()) {
            gain->setParameter("factor", factor);
        }
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setVolume(std::min(1.0, static_cast<double>(factor)));
#endif
    });

    // 节点画布预设按钮
    connect(m_patchbayView->patchbayScene(), &isac::ui::PatchbayScene::topologyChanged,
            this, [lblPatchStatus](const QString& msg) { lblPatchStatus->setText(msg); });

    connect(btnStereo, &QPushButton::clicked, this, [this, lblPatchStatus]() {
        auto src = m_audioEngine.sourceNodeId();
        const auto gainNode = m_audioEngine.gainNode();
        const auto eqNode = m_audioEngine.equalizerNode();
        if (!gainNode || !eqNode) {
            lblPatchStatus->setText("预设无法应用：Gain 或 EQ 插件未加载");
            return;
        }
        auto mul = gainNode->id();
        auto eq = eqNode->id();
        auto snk = m_audioEngine.sinkNodeId();
        if (!m_audioEngine.hasNode(mul) || !m_audioEngine.hasNode(eq)) {
            lblPatchStatus->setText("预设无法应用：增益或 EQ 模块已被删除");
            return;
        }
        m_audioEngine.clearConnections();
        m_audioEngine.connect(src, 0, mul, 0);
        m_audioEngine.connect(src, 1, mul, 1);
        m_audioEngine.connect(mul, 0, eq, 0);
        m_audioEngine.connect(mul, 1, eq, 1);
        m_audioEngine.connect(eq, 0, snk, 0);
        m_audioEngine.connect(eq, 1, snk, 1);
        m_audioEngine.rebuildGraph();
        m_patchbayView->patchbayScene()->syncFromEngine();
    });
    connect(btnSwapLR, &QPushButton::clicked, this, [this, lblPatchStatus]() {
        auto src = m_audioEngine.sourceNodeId();
        const auto gainNode = m_audioEngine.gainNode();
        const auto eqNode = m_audioEngine.equalizerNode();
        if (!gainNode || !eqNode) {
            lblPatchStatus->setText("预设无法应用：Gain 或 EQ 插件未加载");
            return;
        }
        auto mul = gainNode->id();
        auto eq = eqNode->id();
        auto snk = m_audioEngine.sinkNodeId();
        if (!m_audioEngine.hasNode(mul) || !m_audioEngine.hasNode(eq)) {
            lblPatchStatus->setText("预设无法应用：增益或 EQ 模块已被删除");
            return;
        }
        m_audioEngine.clearConnections();
        m_audioEngine.connect(src, 0, mul, 0);
        m_audioEngine.connect(src, 1, mul, 1);
        m_audioEngine.connect(mul, 0, eq, 0);
        m_audioEngine.connect(mul, 1, eq, 1);
        m_audioEngine.connect(eq, 0, snk, 1);
        m_audioEngine.connect(eq, 1, snk, 0);
        m_audioEngine.rebuildGraph();
        m_patchbayView->patchbayScene()->syncFromEngine();
    });
    connect(btnMonoRight, &QPushButton::clicked, this, [this, lblPatchStatus]() {
        auto src = m_audioEngine.sourceNodeId();
        const auto gainNode = m_audioEngine.gainNode();
        const auto addNode = m_audioEngine.addNodePtr();
        const auto eqNode = m_audioEngine.equalizerNode();
        if (!gainNode || !addNode || !eqNode) {
            lblPatchStatus->setText("预设无法应用：依赖插件未加载");
            return;
        }
        auto mul = gainNode->id();
        auto add = addNode->id();
        auto eq = eqNode->id();
        auto snk = m_audioEngine.sinkNodeId();
        if (!m_audioEngine.hasNode(mul) || !m_audioEngine.hasNode(add) ||
            !m_audioEngine.hasNode(eq)) {
            lblPatchStatus->setText("预设无法应用：依赖模块已被删除");
            return;
        }
        m_audioEngine.clearConnections();
        m_audioEngine.connect(src, 0, mul, 0);
        m_audioEngine.connect(src, 1, mul, 1);
        m_audioEngine.connect(mul, 0, add, 0);
        m_audioEngine.connect(mul, 1, add, 1);
        m_audioEngine.connect(add, 0, eq, 0);
        m_audioEngine.connect(add, 0, eq, 1);
        m_audioEngine.connect(eq, 0, snk, 1);
        m_audioEngine.connect(eq, 1, snk, 0);
        m_audioEngine.rebuildGraph();
        m_patchbayView->patchbayScene()->syncFromEngine();
    });
    connect(btnClear, &QPushButton::clicked, this, [this]() {
        m_audioEngine.clearConnections();
        m_audioEngine.rebuildGraph();
        m_patchbayView->patchbayScene()->syncFromEngine();
    });

    // 进度条刷新定时器 (每 200ms 同步一次底层解码器帧游标)
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        if (m_audioEngine.isPlaying()) {
            const double position = m_audioEngine.currentPositionSeconds();
            const double duration = m_audioEngine.totalDurationSeconds();
            m_nowPlayingBar->updateProgress(
                position, duration);
#ifdef ISAC_HAS_MPRIS
            m_mprisController->setPosition(position);
#endif
            if (duration > 0.0 && position >= duration &&
                !m_currentTrackEndHandled) {
                m_currentTrackEndHandled = true;
                if (m_playbackMode == isac::library::PlaybackMode::RepeatOne) {
                    m_audioEngine.seekToSeconds(0.0);
                    m_currentTrackEndHandled = false;
                    m_nowPlayingBar->updateProgress(
                        0.0, m_audioEngine.totalDurationSeconds());
                } else {
                    const auto next = m_playbackQueue.next(true);
                    if (next) {
                        playQueuedTrack(*next);
                    } else {
                        m_audioEngine.setPlaying(false);
                        m_audioEngine.seekToSeconds(0.0);
                        m_currentTrackEndHandled = false;
                        m_nowPlayingBar->setPlayingState(false);
#ifdef ISAC_HAS_MPRIS
                        m_mprisController->setPlaybackState(false);
#endif
                        m_nowPlayingBar->updateProgress(
                            0.0, m_audioEngine.totalDurationSeconds());
                    }
                }
            }
        }
    });
    m_progressTimer->start(200);
}

void MainWindow::playTrackById(qint64 trackId) {
    rebuildPlaybackQueue(trackId);
    playQueuedTrack(trackId);
}

void MainWindow::rebuildPlaybackQueue(qint64 currentTrackId) {
    std::vector<qint64> trackIds = m_libraryPage->trackIds();
    m_playbackQueue.reset(std::move(trackIds), currentTrackId, m_playbackMode);
}

void MainWindow::playQueuedTrack(qint64 trackId) {
    const auto track = m_libraryRepository.trackById(trackId);
    if (!track) {
        return;
    }

    if (m_audioEngine.loadAndPlay(track->filePath.toStdString())) {
        m_currentTrackEndHandled = false;
        m_currentTrackId = trackId;
        m_libraryPage->updateHeroBanner(*track);
        m_nowPlayingBar->setTrackInfo(*track);
        m_nowPlayingBar->setPlayingState(true);
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setTrack(*track);
        m_mprisController->setPosition(0.0);
        m_mprisController->setPlaybackState(true);
#endif
    }
}

MainWindow::~MainWindow() {
    delete ui;
}
