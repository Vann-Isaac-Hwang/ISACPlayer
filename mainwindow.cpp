#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "ui/inspector/NodeInspectorWidget.h"
#include "ui/patchbay/DspPresetCatalog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QListWidget>
#include <QCoreApplication>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSettings>
#include <QSignalBlocker>
#include <QMessageBox>
#include <algorithm>

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
    QSettings settings;
    m_audioEngine.setFadeEnabled(
        settings.value(QStringLiteral("playback/fadeEnabled"), false).toBool());
    m_audioEngine.setFadeDurationSeconds(
        settings.value(QStringLiteral("playback/fadeDurationSeconds"), 2.0).toDouble());
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

    // ==================== 1. 左侧页面导航栏 ====================
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(88);
    sidebar->setStyleSheet(R"(
        QWidget#sidebar {
            background-color: #191B22;
            border-right: 1px solid #262934;
        }
        QPushButton {
            text-align: center; padding: 0; margin: 0; border: none;
            border-radius: 0; color: #E6E8ED; font-size: 11px;
            font-weight: 500; background: #292C34;
        }
        QPushButton:hover { background: #454952; color: white; }
        QPushButton:pressed { background: #565A64; }
        QPushButton:checked { background: #393C45; color: white; font-weight: bold; }
    )");

    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    auto* btnNavLibrary = new QPushButton("音乐资料库", sidebar);
    auto* btnNavPlaylists = new QPushButton("歌单管理", sidebar);
    auto* btnNavPatchbay = new QPushButton("DSP Router", sidebar);
    btnNavLibrary->setCheckable(true);
    btnNavPlaylists->setCheckable(true);
    btnNavPatchbay->setCheckable(true);
    btnNavLibrary->setFixedHeight(44);
    btnNavPlaylists->setFixedHeight(44);
    btnNavPatchbay->setFixedHeight(44);
    btnNavLibrary->setChecked(true);

    sideLayout->addWidget(btnNavLibrary);
    sideLayout->addWidget(btnNavPlaylists);
    sideLayout->addWidget(btnNavPatchbay);
    sideLayout->addStretch();
    auto* settingsButton = new QPushButton("设置", sidebar);
    settingsButton->setFixedHeight(44);
    settingsButton->setToolTip("播放设置");
    sideLayout->addWidget(settingsButton);
    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        QSettings settings;
        QDialog dialog(this);
        dialog.setWindowTitle("播放设置");
        dialog.setMinimumWidth(360);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();
        auto* fadeEnabled = new QCheckBox("启用淡入淡出", &dialog);
        fadeEnabled->setChecked(
            settings.value(QStringLiteral("playback/fadeEnabled"), false).toBool());
        auto* fadeDuration = new QDoubleSpinBox(&dialog);
        fadeDuration->setRange(0.25, 10.0);
        fadeDuration->setSingleStep(0.25);
        fadeDuration->setSuffix(" 秒");
        fadeDuration->setDecimals(2);
        fadeDuration->setValue(
            settings.value(QStringLiteral("playback/fadeDurationSeconds"), 2.0)
                .toDouble());
        fadeDuration->setEnabled(fadeEnabled->isChecked());
        connect(fadeEnabled, &QCheckBox::toggled,
                fadeDuration, &QDoubleSpinBox::setEnabled);
        form->addRow(fadeEnabled);
        form->addRow("淡入/淡出时长", fadeDuration);
        auto makeInfoOption = [&settings, &dialog](const QString& label,
                                                   const QString& key) {
            auto* option = new QCheckBox(&dialog);
            option->setChecked(settings.value(key, true).toBool());
            return std::pair{label, option};
        };
        const auto [formatLabel, showFormat] =
            makeInfoOption("文件格式", QStringLiteral("display/audioFormat"));
        const auto [sampleRateLabel, showSampleRate] =
            makeInfoOption("采样率", QStringLiteral("display/sampleRate"));
        const auto [bitDepthLabel, showBitDepth] =
            makeInfoOption("位深", QStringLiteral("display/bitDepth"));
        const auto [channelsLabel, showChannels] =
            makeInfoOption("声道数", QStringLiteral("display/channels"));
        const auto [bitrateLabel, showBitrate] =
            makeInfoOption("比特率", QStringLiteral("display/bitrate"));
        form->addRow(new QLabel("曲目信息显示", &dialog));
        form->addRow(formatLabel, showFormat);
        form->addRow(sampleRateLabel, showSampleRate);
        form->addRow(bitDepthLabel, showBitDepth);
        form->addRow(channelsLabel, showChannels);
        form->addRow(bitrateLabel, showBitrate);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        settings.setValue(QStringLiteral("playback/fadeEnabled"),
                          fadeEnabled->isChecked());
        settings.setValue(QStringLiteral("playback/fadeDurationSeconds"),
                          fadeDuration->value());
        m_audioEngine.setFadeDurationSeconds(fadeDuration->value());
        m_audioEngine.setFadeEnabled(fadeEnabled->isChecked());
        settings.setValue(QStringLiteral("display/audioFormat"),
                          showFormat->isChecked());
        settings.setValue(QStringLiteral("display/sampleRate"),
                          showSampleRate->isChecked());
        settings.setValue(QStringLiteral("display/bitDepth"),
                          showBitDepth->isChecked());
        settings.setValue(QStringLiteral("display/channels"),
                          showChannels->isChecked());
        settings.setValue(QStringLiteral("display/bitrate"),
                          showBitrate->isChecked());
        m_nowPlayingHeader->setAudioInfoVisibility(
            showFormat->isChecked(), showSampleRate->isChecked(),
            showBitDepth->isChecked(), showChannels->isChecked(),
            showBitrate->isChecked());
    });

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
    m_playlistPage = new isac::ui::PlaylistPage(&m_libraryRepository, this);
    connect(m_playlistPage, &isac::ui::PlaylistPage::trackDoubleClicked,
            this, &MainWindow::playTrackById);

    // 页面 2：Coppwr 风格 DSP 连线调音台页面
    auto* patchbayContainer = new QWidget(this);
    auto* patchLayout = new QVBoxLayout(patchbayContainer);
    patchLayout->setContentsMargins(24, 20, 24, 16);
    patchLayout->setSpacing(12);

    auto* patchHeader = new QHBoxLayout();
    auto* patchStatusRow = new QHBoxLayout();
    auto* presetSelector = new QComboBox(this);
    presetSelector->setMinimumWidth(180);
    presetSelector->setToolTip("选择 DSP 路由配置");
    presetSelector->setStyleSheet(
        "QComboBox { background: #1E2028; color: #EAEAEA; "
        "border: 1px solid #343846; border-radius: 5px; padding: 6px 8px; }");
    auto* clearConnectionsButton = new QPushButton("清空连线", this);
    clearConnectionsButton->setToolTip("移除画布上的全部连线");
    clearConnectionsButton->setStyleSheet(
        "QPushButton { background: #252834; color: #E0E4F0; "
        "border: 1px solid #343846; border-radius: 5px; padding: 6px 10px; }");
    auto* lblPatchStatus = new QLabel(this);
    lblPatchStatus->setMinimumWidth(80);
    lblPatchStatus->setStyleSheet("color: #9499A6; font-size: 12px;");
    auto* outputSelector = new QComboBox(this);
    outputSelector->setMinimumWidth(190);
    outputSelector->setToolTip("播放音频使用的输出设备");
    outputSelector->setStyleSheet(
        "QComboBox { background: #1E2028; color: #EAEAEA; "
        "border: 1px solid #343846; border-radius: 5px; padding: 6px 8px; }");
    auto* refreshOutputsButton = new QPushButton("刷新设备", this);
    refreshOutputsButton->setToolTip("重新枚举音频输出设备");
    refreshOutputsButton->setStyleSheet(
        "QPushButton { background: #252834; color: #E0E4F0; "
        "border: 1px solid #343846; border-radius: 5px; padding: 6px 9px; }");
    auto* outputStatus = new QLabel(this);
    outputStatus->setStyleSheet("color: #9499A6; font-size: 12px;");
    patchHeader->addWidget(new QLabel("配置", this));
    patchHeader->addWidget(presetSelector);
    patchHeader->addWidget(clearConnectionsButton);
    patchHeader->addSpacing(16);
    patchHeader->addWidget(new QLabel("音频输出", this));
    patchHeader->addWidget(outputSelector);
    patchHeader->addWidget(refreshOutputsButton);
    patchHeader->addStretch();
    patchStatusRow->addWidget(outputStatus);
    patchStatusRow->addStretch();
    patchStatusRow->addWidget(lblPatchStatus);

    m_patchbayView = new isac::ui::PatchbayView(m_audioEngine, this);
    m_patchbayView->setStyleSheet("border: 1px solid #2B2F3C; border-radius: 12px;");

    // ==================== 3. 音频路由页内的 DSP 模块库 ====================
    auto* pluginPanel = new QWidget(patchbayContainer);
    pluginPanel->setMinimumWidth(240);
    pluginPanel->setMaximumWidth(280);
    pluginPanel->setStyleSheet(R"(
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

    auto* pluginLayout = new QVBoxLayout(pluginPanel);
    pluginLayout->setContentsMargins(10, 10, 10, 10);
    pluginLayout->setSpacing(8);

    auto* pluginTitle = new QLabel("DSP 模块库", pluginPanel);
    pluginTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #E5E7EF;");
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

    pluginLayout->addWidget(pluginTitle);
    pluginLayout->addWidget(pluginList, 1);
    pluginLayout->addWidget(pluginDescription);
    pluginLayout->addWidget(instantiateButton);

    auto* inspectorPanel = new QWidget(patchbayContainer);
    inspectorPanel->setStyleSheet(
        "QWidget { background: #191B22; color: #D0D4E0; }");
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(10, 10, 10, 10);
    inspectorLayout->setSpacing(8);
    auto* inspectorTitle = new QLabel("节点检视器", inspectorPanel);
    inspectorTitle->setStyleSheet(
        "font-size: 14px; font-weight: bold; color: #E5E7EF;");
    auto* inspector = new isac::ui::NodeInspectorWidget(inspectorPanel);
    inspectorLayout->addWidget(inspectorTitle);
    inspectorLayout->addWidget(inspector, 1);

    auto* sidePanel = new QSplitter(Qt::Vertical, patchbayContainer);
    sidePanel->setChildrenCollapsible(false);
    sidePanel->setMinimumWidth(240);
    sidePanel->setMaximumWidth(280);
    sidePanel->setStyleSheet(
        "QSplitter::handle:vertical { height: 5px; background: #343846; }");
    sidePanel->addWidget(pluginPanel);
    sidePanel->addWidget(inspectorPanel);
    sidePanel->setStretchFactor(0, 3);
    sidePanel->setStretchFactor(1, 2);

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

    patchLayout->addLayout(patchHeader);
    patchLayout->addLayout(patchStatusRow);
    auto* patchbayBody = new QHBoxLayout();
    patchbayBody->setSpacing(12);
    patchbayBody->addWidget(m_patchbayView, 1);
    patchbayBody->addWidget(sidePanel);
    patchLayout->addLayout(patchbayBody, 1);

    m_stackedWidget->addWidget(m_libraryPage);
    m_stackedWidget->addWidget(m_playlistPage);
    m_stackedWidget->addWidget(patchbayContainer);

    auto refreshOutputDevices = [this, outputSelector, outputStatus]() {
        const QSignalBlocker blocker(outputSelector);
        QSettings settings;
        QString status;
        const QByteArray savedId =
            settings.value(QStringLiteral("audio/outputDeviceId")).toByteArray();
        const auto devices = m_audioEngine.availableOutputDevices();
        outputSelector->clear();
        int selectedIndex = 0;
        for (const auto& device : devices) {
            const QByteArray id = device.useSystemDefault
                ? QByteArray{}
                : QByteArray(reinterpret_cast<const char*>(device.id.data()),
                             static_cast<qsizetype>(device.id.size()));
            QString label = QString::fromStdString(device.name);
            if (!device.useSystemDefault && device.isDefault) {
                label += QStringLiteral(" (系统默认)");
            }
            outputSelector->addItem(label, id);
            outputSelector->setItemData(
                outputSelector->count() - 1,
                QStringLiteral("检测到的最大通道数：%1；当前输出为立体声")
                    .arg(device.maxChannels),
                Qt::ToolTipRole);
            if (id == savedId) {
                selectedIndex = outputSelector->count() - 1;
            }
        }
        outputSelector->setCurrentIndex(selectedIndex);
        if (!savedId.isEmpty() && selectedIndex == 0) {
            settings.setValue(QStringLiteral("audio/outputDeviceId"), QByteArray{});
        }
        if (!devices.empty()) {
            const auto& desired = devices[static_cast<std::size_t>(selectedIndex)];
            const auto current = m_audioEngine.selectedOutputDevice();
            const QByteArray currentId = current
                ? QByteArray(reinterpret_cast<const char*>(current->id.data()),
                             static_cast<qsizetype>(current->id.size()))
                : QByteArray{};
            if (currentId != outputSelector->currentData().toByteArray()) {
                std::string error;
                if (!m_audioEngine.selectOutputDevice(desired, error)) {
                    status = QStringLiteral("无法应用输出设备：%1")
                                 .arg(QString::fromStdString(error));
                    outputSelector->setCurrentIndex(0);
                    settings.setValue(
                        QStringLiteral("audio/outputDeviceId"), QByteArray{});
                }
            }
        }
        if (status.isEmpty()) {
            status = QStringLiteral("%1 个设备 | 引擎请求 %2 kHz")
                         .arg(std::max(0, outputSelector->count() - 1))
                         .arg(m_audioEngine.outputSampleRate() / 1000.0, 0, 'g', 4);
        }
        outputStatus->setText(status);
    };
    refreshOutputDevices();
    connect(refreshOutputsButton, &QPushButton::clicked,
            this, refreshOutputDevices);
    connect(outputSelector, QOverload<int>::of(&QComboBox::activated),
            this, [this, outputSelector, outputStatus, refreshOutputDevices]() {
        const QByteArray id = outputSelector->currentData().toByteArray();
        auto devices = m_audioEngine.availableOutputDevices();
        const auto selected = std::find_if(
            devices.begin(), devices.end(),
            [&id](const isac::AudioOutputDevice& device) {
                if (device.useSystemDefault) {
                    return id.isEmpty();
                }
                return id == QByteArray(
                    reinterpret_cast<const char*>(device.id.data()),
                    static_cast<qsizetype>(device.id.size()));
            });
        if (selected == devices.end()) {
            outputStatus->setText("设备已不存在，请刷新列表");
            return;
        }
        std::string error;
        if (!m_audioEngine.selectOutputDevice(*selected, error)) {
            refreshOutputDevices();
            outputStatus->setText(
                QStringLiteral("切换失败，已保留原输出：%1")
                    .arg(QString::fromStdString(error)));
            return;
        }
        QSettings settings;
        settings.setValue(QStringLiteral("audio/outputDeviceId"), id);
        outputStatus->setText(
            QStringLiteral("当前输出：%1（立体声，DSP 请求 %2 kHz）")
                .arg(QString::fromStdString(selected->name))
                .arg(m_audioEngine.outputSampleRate() / 1000.0, 0, 'g', 4));
    });

    upperLayout->addWidget(sidebar);
    upperLayout->addWidget(m_stackedWidget, 1);

    // ==================== 3. 顶部常驻播放工作区 ====================
    m_nowPlayingHeader = new isac::ui::NowPlayingHeader(this);
    m_nowPlayingHeader->setAudioInfoVisibility(
        settings.value(QStringLiteral("display/audioFormat"), true).toBool(),
        settings.value(QStringLiteral("display/sampleRate"), true).toBool(),
        settings.value(QStringLiteral("display/bitDepth"), true).toBool(),
        settings.value(QStringLiteral("display/channels"), true).toBool(),
        settings.value(QStringLiteral("display/bitrate"), true).toBool());
    restorePlaybackQueue();

    rootLayout->addWidget(m_nowPlayingHeader);
    rootLayout->addLayout(upperLayout, 1);

    // ==================== 信号与槽绑定 ====================
    connect(btnNavLibrary, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPlaylists, btnNavPatchbay]() {
        btnNavLibrary->setChecked(true);
        btnNavPlaylists->setChecked(false);
        btnNavPatchbay->setChecked(false);
        m_stackedWidget->setCurrentIndex(0);
    });
    connect(btnNavPlaylists, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPlaylists, btnNavPatchbay]() {
        btnNavLibrary->setChecked(false);
        btnNavPlaylists->setChecked(true);
        btnNavPatchbay->setChecked(false);
        m_playlistPage->refresh();
        m_stackedWidget->setCurrentIndex(1);
    });
    connect(btnNavPatchbay, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPlaylists, btnNavPatchbay]() {
        btnNavLibrary->setChecked(false);
        btnNavPlaylists->setChecked(false);
        btnNavPatchbay->setChecked(true);
        m_stackedWidget->setCurrentIndex(2);
    });

#ifdef ISAC_HAS_MPRIS
    m_mprisController = new isac::platform::MprisController(this);
    m_mprisController->setPlaybackMode(m_playbackMode);
    if (m_currentTrackId > 0) {
        if (const auto track = m_libraryRepository.trackById(m_currentTrackId)) {
            m_mprisController->setTrack(*track);
            m_mprisController->setPlaybackState(false);
        }
    }
    if (!m_mprisController->isAvailable()) {
        qWarning() << "Unable to register the KDE MPRIS D-Bus service";
    }
    connect(m_mprisController, &isac::platform::MprisController::playRequested,
            this, [this]() {
                if (m_trackLoaded) {
                    m_audioEngine.setPlaying(true);
                    m_nowPlayingHeader->setPlayingState(m_audioEngine.isPlaying());
                    m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
                    return;
                }
                if (const auto current = m_playbackQueue.currentTrackId()) {
                    playQueuedTrack(*current);
                    return;
                }
                const auto ids = currentTrackIds();
                if (!ids.empty()) {
                    playTrackById(ids.front());
                }
            });
    connect(m_mprisController, &isac::platform::MprisController::pauseRequested,
            this, [this]() {
                m_audioEngine.setPlaying(false);
                m_nowPlayingHeader->setPlayingState(false);
                m_mprisController->setPlaybackState(false);
            });
    connect(m_mprisController, &isac::platform::MprisController::playPauseRequested,
            this, [this]() {
                if (!m_trackLoaded) {
                    if (const auto current = m_playbackQueue.currentTrackId()) {
                        playQueuedTrack(*current);
                        return;
                    }
                    const auto ids = currentTrackIds();
                    if (!ids.empty()) {
                        playTrackById(ids.front());
                    }
                    return;
                }
                const bool playing = !m_audioEngine.isPlaying();
                m_audioEngine.setPlaying(playing);
                m_nowPlayingHeader->setPlayingState(m_audioEngine.isPlaying());
                m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
            });
    connect(m_mprisController, &isac::platform::MprisController::stopRequested,
            this, [this]() {
                m_audioEngine.setPlaying(false);
                m_audioEngine.seekToSeconds(0.0);
                m_nowPlayingHeader->setPlayingState(false);
                m_nowPlayingHeader->updateProgress(
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
                m_nowPlayingHeader->setVolume(volume);
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
                persistPlaybackQueue(true);
                m_nowPlayingHeader->setPlaybackMode(mode);
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

    // 常驻播放工作区事件
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::playPauseClicked, this, [this]() {
        if (!m_trackLoaded) {
            if (const auto current = m_playbackQueue.currentTrackId()) {
                playQueuedTrack(*current);
                return;
            }
            const auto ids = currentTrackIds();
            if (!ids.empty()) {
                playTrackById(ids.front());
            }
            return;
        }
        bool nextState = !m_audioEngine.isPlaying();
        m_audioEngine.setPlaying(nextState);
        m_nowPlayingHeader->setPlayingState(m_audioEngine.isPlaying());
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setPlaybackState(m_audioEngine.isPlaying());
#endif
    });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::prevClicked, this, [this]() {
        const auto previous = m_playbackQueue.previous();
        if (previous) {
            playQueuedTrack(*previous);
        }
    });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::nextClicked, this, [this]() {
        const auto next = m_playbackQueue.next();
        if (next) {
            playQueuedTrack(*next);
        }
    });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::playbackModeChanged,
            this, [this](int mode) {
                m_playbackMode = static_cast<isac::library::PlaybackMode>(mode);
                m_playbackQueue.setMode(m_playbackMode);
                persistPlaybackQueue(true);
#ifdef ISAC_HAS_MPRIS
                m_mprisController->setPlaybackMode(m_playbackMode);
#endif
            });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::seekRequested, this, [this](double sec) {
        m_audioEngine.seekToSeconds(sec);
#ifdef ISAC_HAS_MPRIS
        m_mprisController->notifySeeked(m_audioEngine.currentPositionSeconds());
#endif
    });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::volumeChanged, this, [this](float factor) {
        if (const auto gain = m_audioEngine.gainNode()) {
            gain->setParameter("factor", factor);
        }
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setVolume(std::min(1.0, static_cast<double>(factor)));
#endif
    });

    // Preset files are copied once into the user's writable config directory.
    connect(m_patchbayView->patchbayScene(), &isac::ui::PatchbayScene::topologyChanged,
            this, [lblPatchStatus](const QString& msg) { lblPatchStatus->setText(msg); });

    const QString presetDirectory = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
            .filePath(QStringLiteral("dsp-presets"));
    presetSelector->setToolTip(
        QStringLiteral("DSP 配置目录：%1").arg(presetDirectory));
    QString presetError;
    if (!isac::ui::DspPresetCatalog::installBuiltIns(
            presetDirectory, &presetError)) {
        lblPatchStatus->setText(
            QStringLiteral("内置配置安装失败：%1").arg(presetError));
    }
    const auto presets =
        isac::ui::DspPresetCatalog::load(presetDirectory, &presetError);
    presetSelector->addItem(QStringLiteral("选择路由配置"));
    for (const auto& preset : presets) {
        presetSelector->addItem(preset.name, preset.id);
    }
    if (!presetError.isEmpty()) {
        lblPatchStatus->setText(
            QStringLiteral("部分配置无法读取：%1").arg(presetError));
    }

    connect(presetSelector, QOverload<int>::of(&QComboBox::activated),
            this, [this, presetSelector, lblPatchStatus, presets]() {
        const QString selectedId = presetSelector->currentData().toString();
        const auto selectedPreset = std::find_if(
            presets.begin(), presets.end(),
            [&selectedId](const isac::ui::DspPreset& preset) {
                return preset.id == selectedId;
            });
        if (selectedPreset == presets.end()) {
            return;
        }

        QHash<QString, isac::NodeId> roleNodes{
            {QStringLiteral("source"), m_audioEngine.sourceNodeId()},
            {QStringLiteral("sink"), m_audioEngine.sinkNodeId()}
        };
        if (const auto node = m_audioEngine.gainNode()) {
            roleNodes.insert(QStringLiteral("gain"), node->id());
        }
        if (const auto node = m_audioEngine.addNodePtr()) {
            roleNodes.insert(QStringLiteral("adder"), node->id());
        }
        if (const auto node = m_audioEngine.equalizerNode()) {
            roleNodes.insert(QStringLiteral("eq"), node->id());
        }

        QString error;
        if (!isac::ui::DspPresetCatalog::apply(
                *selectedPreset, m_audioEngine, roleNodes, &error)) {
            lblPatchStatus->setText(
                QStringLiteral("配置未应用：%1").arg(error));
            return;
        }
        m_patchbayView->patchbayScene()->syncFromEngine();
        lblPatchStatus->setText(
            QStringLiteral("已加载配置：%1").arg(selectedPreset->name));
    });

    connect(clearConnectionsButton, &QPushButton::clicked, this, [this, lblPatchStatus]() {
        m_audioEngine.clearConnections();
        if (!m_audioEngine.rebuildGraph()) {
            lblPatchStatus->setText("清空连线后图编译失败");
            return;
        }
        m_patchbayView->patchbayScene()->syncFromEngine();
        lblPatchStatus->setText("连线已清空");
    });

    // 进度条刷新定时器 (每 200ms 同步一次底层解码器帧游标)
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this, outputStatus]() {
        const auto selectedOutput = m_audioEngine.selectedOutputDevice();
        const QString outputName = selectedOutput
            ? QString::fromStdString(selectedOutput->name)
            : QStringLiteral("系统默认");
        outputStatus->setText(
            QStringLiteral("%1 | DSP 请求 %2 kHz")
                .arg(outputName)
                .arg(m_audioEngine.outputSampleRate() / 1000.0, 0, 'g', 4));
        if (m_audioEngine.isPlaying()) {
            const double position = m_audioEngine.currentPositionSeconds();
            const double duration = m_audioEngine.totalDurationSeconds();
            m_nowPlayingHeader->updateProgress(
                position, duration);
#ifdef ISAC_HAS_MPRIS
            m_mprisController->setPosition(position);
#endif
            if (m_audioEngine.isPlaybackComplete() &&
                !m_currentTrackEndHandled) {
                m_currentTrackEndHandled = true;
                if (m_playbackMode == isac::library::PlaybackMode::RepeatOne) {
                    m_audioEngine.seekToSeconds(0.0);
                    m_currentTrackEndHandled = false;
                    m_nowPlayingHeader->updateProgress(
                        0.0, m_audioEngine.totalDurationSeconds());
                } else {
                    const bool startedNextTrack =
                        m_playbackQueue.advanceToPlayable(
                            true, [this](qint64 id) {
                                return playQueuedTrack(id);
                            });
                    if (!startedNextTrack) {
                        m_audioEngine.setPlaying(false);
                        if (m_audioEngine.totalDurationSeconds() > 0.0) {
                            m_audioEngine.seekToSeconds(0.0);
                        }
                        m_currentTrackEndHandled = false;
                        m_nowPlayingHeader->setPlayingState(false);
#ifdef ISAC_HAS_MPRIS
                        m_mprisController->setPlaybackState(false);
#endif
                        m_nowPlayingHeader->updateProgress(
                            0.0, m_audioEngine.totalDurationSeconds());
                        persistPlaybackQueue(false);
                    }
                }
            }
        }
    });
    m_progressTimer->start(200);

    m_visualizationTimer = new QTimer(this);
    m_visualizationTimer->setTimerType(Qt::PreciseTimer);
    connect(m_visualizationTimer, &QTimer::timeout, this, [this]() {
        const auto preFrames = m_audioEngine.readPreDspSamples(
            m_preVisualizationLeft.data(), m_preVisualizationRight.data(),
            m_preVisualizationLeft.size());
        const auto postFrames = m_audioEngine.readPostDspSamples(
            m_postVisualizationLeft.data(), m_postVisualizationRight.data(),
            m_postVisualizationLeft.size());
        const auto frames = std::min(preFrames, postFrames);
        if (frames == 0) {
            return;
        }
        m_nowPlayingHeader->updateVisualization(
            m_preVisualizationLeft.data(), m_preVisualizationRight.data(),
            m_postVisualizationLeft.data(), m_postVisualizationRight.data(),
            frames, m_audioEngine.outputSampleRate());
    });
    m_visualizationTimer->start(33);
}

void MainWindow::playTrackById(qint64 trackId) {
    rebuildPlaybackQueue(trackId);
    if (!playQueuedTrack(trackId)) {
        QMessageBox::warning(
            this, QStringLiteral("Playback failed"),
            m_lastPlaybackError.isEmpty()
                ? QStringLiteral("The selected track could not be played.")
                : m_lastPlaybackError);
    }
}

void MainWindow::rebuildPlaybackQueue(qint64 currentTrackId) {
    std::vector<qint64> trackIds = currentTrackIds();
    m_playbackQueue.reset(std::move(trackIds), currentTrackId, m_playbackMode);
    m_playbackQueueNeedsSave = true;
}

std::vector<qint64> MainWindow::currentTrackIds() const {
    if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 &&
        m_playlistPage) {
        return m_playlistPage->trackIds();
    }
    return m_libraryPage ? m_libraryPage->trackIds()
                         : std::vector<qint64>{};
}

bool MainWindow::playQueuedTrack(qint64 trackId) {
    const auto track = m_libraryRepository.trackById(trackId);
    if (!track) {
        return false;
    }

    std::string playbackError;
    if (m_audioEngine.loadAndPlay(
            track->filePath.toStdString(), &playbackError)) {
        m_lastPlaybackError.clear();
        auto displayTrack = *track;
        isac::library::MetadataReader::readAudioProperties(displayTrack);
        m_currentTrackEndHandled = false;
        m_currentTrackId = trackId;
        m_trackLoaded = true;
        m_nowPlayingHeader->setTrackInfo(displayTrack);
        m_nowPlayingHeader->setPlayingState(true);
#ifdef ISAC_HAS_MPRIS
        m_mprisController->setTrack(*track);
        m_mprisController->setPosition(0.0);
        m_mprisController->setPlaybackState(true);
#endif
        persistPlaybackQueue(m_playbackQueueNeedsSave);
        return true;
    }
    m_lastPlaybackError = QString::fromStdString(playbackError);
    m_trackLoaded = false;
    m_currentTrackEndHandled = false;
    m_nowPlayingHeader->setPlayingState(false);
#ifdef ISAC_HAS_MPRIS
    m_mprisController->setPlaybackState(false);
#endif
    qWarning() << "Unable to play track"
               << track->filePath << QString::fromStdString(playbackError);
    return false;
}

void MainWindow::persistPlaybackQueue(bool includeOrder) {
    if (includeOrder) {
        m_playbackQueueNeedsSave = !m_libraryRepository.savePlaybackQueue(
            m_playbackQueue.orderedTrackIds(), m_playbackQueue.position(),
            m_playbackMode);
    } else {
        if (!m_libraryRepository.savePlaybackCursor(
                m_playbackQueue.position(), m_playbackMode)) {
            m_playbackQueueNeedsSave = !m_libraryRepository.savePlaybackQueue(
                m_playbackQueue.orderedTrackIds(),
                m_playbackQueue.position(), m_playbackMode);
        }
    }
}

void MainWindow::restorePlaybackQueue() {
    const auto saved = m_libraryRepository.loadPlaybackQueue();
    if (!saved) {
        return;
    }
    m_playbackMode = saved->mode;
    m_playbackQueue.restore(
        saved->trackIds, saved->position, saved->mode);
    m_currentTrackId =
        m_playbackQueue.currentTrackId().value_or(0);
    m_nowPlayingHeader->setPlaybackMode(static_cast<int>(m_playbackMode));
    if (m_currentTrackId > 0) {
        if (const auto track = m_libraryRepository.trackById(m_currentTrackId)) {
            auto displayTrack = *track;
            isac::library::MetadataReader::readAudioProperties(displayTrack);
            m_nowPlayingHeader->setTrackInfo(displayTrack);
        } else {
            m_currentTrackId = 0;
        }
    }
}

MainWindow::~MainWindow() {
    delete ui;
}
