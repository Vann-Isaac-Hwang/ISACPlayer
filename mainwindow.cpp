#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "ui/inspector/NodeInspectorWidget.h"
#include "ui/patchbay/DspPresetCatalog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
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
#include <QSpinBox>
#include <QFormLayout>
#include <QDockWidget>
#include <QSettings>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QAbstractItemView>
#include <QScrollArea>
#include <QSet>
#include <QMenu>
#include <QMenuBar>
#include <QKeySequence>
#include <QMetaObject>
#include <QRegularExpression>
#ifdef ISAC_HAS_MPRIS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif
#include <algorithm>

namespace {

QString libraryDatabasePath()
{
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDirectory);
    return QDir(dataDirectory).filePath(QStringLiteral("library.sqlite"));
}

int normalizedVisualizationFftSize(int value)
{
    constexpr std::array<int, 4> supportedSizes{
        2048, 4096, 8192, 16384};
    const auto closest = std::min_element(
        supportedSizes.cbegin(), supportedSizes.cend(),
        [value](int left, int right) {
            return std::abs(left - value) < std::abs(right - value);
        });
    return *closest;
}

int normalizedVisualizationUpdateInterval(int value)
{
    return std::clamp(value, 16, 200);
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_themeManager(this)
    , ui(new Ui::MainWindow)
    , m_pluginManager(QCoreApplication::applicationDirPath() + "/plugins")
    , m_formatPluginManager(
          QDir(QCoreApplication::applicationDirPath())
              .filePath(QStringLiteral("format_plugins")))
    , m_libraryRepository(libraryDatabasePath())
    , m_audioEngine()
{
    ui->setupUi(this);
    QSettings settings;
    m_audioEngine.setFadeEnabled(
        settings.value(QStringLiteral("playback/fadeEnabled"), false).toBool());
    m_audioEngine.setFadeDurationSeconds(
        settings.value(QStringLiteral("playback/fadeDurationSeconds"), 2.0).toDouble());
    m_audioEngine.setOverlapEnabled(
        settings.value(QStringLiteral("playback/overlapEnabled"), false).toBool());
    m_audioEngine.setOverlapDurationSeconds(
        settings.value(QStringLiteral("playback/overlapDurationSeconds"), 2.0)
            .toDouble());
    setWindowTitle(QStringLiteral("ISACPlayer"));
    resize(1180, 760);

    // 全局暗黑主题背景
    ui->centralwidget->setStyleSheet("background-color: #14161C;");
#if defined(Q_OS_LINUX) && defined(ISAC_HAS_MPRIS)
    bool globalMenuAvailable = false;
    const QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (sessionBus.isConnected() && sessionBus.interface()) {
        const auto registrar = sessionBus.interface()->isServiceRegistered(
            QStringLiteral("com.canonical.AppMenu.Registrar"));
        globalMenuAvailable = registrar.isValid() && registrar.value();
    }
    ui->menubar->setNativeMenuBar(globalMenuAvailable);
#else
    ui->menubar->setNativeMenuBar(false);
#endif
    ui->menubar->setVisible(true);
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

    m_navLibraryButton = new QPushButton("Library", sidebar);
    auto* btnNavLibrary = m_navLibraryButton;
    auto* btnNavPlaylists = new QPushButton("Playlists", sidebar);
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
    auto* settingsButton = new QPushButton("Settings", sidebar);
    settingsButton->setFixedHeight(44);
    settingsButton->setToolTip("Settings");
    sideLayout->addWidget(settingsButton);
    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        QSettings settings;
        QDialog dialog(this);
        dialog.setWindowTitle("Settings");
        dialog.resize(680, 760);
        auto* dialogLayout = new QVBoxLayout(&dialog);
        auto* scrollArea = new QScrollArea(&dialog);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget(scrollArea);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(10);
        auto addSectionTitle = [content, layout](const QString& title) {
            auto* heading = new QLabel(title, content);
            heading->setStyleSheet(
                "font-size: 18px; font-weight: bold; color: #F0F1F4; "
                "padding-top: 8px; border-bottom: 1px solid #343846;");
            layout->addWidget(heading);
        };

        addSectionTitle(QStringLiteral("Appearance"));
        auto* appearanceForm = new QFormLayout();
        auto* themeSelector = new QComboBox(&dialog);
        themeSelector->addItem(QStringLiteral("Follow System"),
                               static_cast<int>(isac::ui::ThemeMode::System));
        themeSelector->addItem(QStringLiteral("Light"),
                               static_cast<int>(isac::ui::ThemeMode::Light));
        themeSelector->addItem(QStringLiteral("Dark"),
                               static_cast<int>(isac::ui::ThemeMode::Dark));
        const int themeIndex = themeSelector->findData(
            static_cast<int>(m_themeManager.mode()));
        themeSelector->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
        appearanceForm->addRow(QStringLiteral("Color theme"), themeSelector);
        layout->addLayout(appearanceForm);

        addSectionTitle(QStringLiteral("Music Library"));
        auto* pathList = new QListWidget(content);
        pathList->setSelectionMode(QAbstractItemView::SingleSelection);
        pathList->setMinimumHeight(170);
        pathList->setStyleSheet(
            "QListWidget { background: #14161C; color: #D0D4E0; "
            "border: 1px solid #343846; }"
            "QListWidget::item { padding: 7px; }"
            "QListWidget::item:selected { background: #343A4B; color: white; }");
        auto* pathStatus = new QLabel(content);
        pathStatus->setStyleSheet("color: #9499A6;");
        auto* pathActionRow = new QHBoxLayout();
        auto* importFilesButton =
            new QPushButton(QStringLiteral("Import Audio Files"), content);
        auto* addFolderButton =
            new QPushButton(QStringLiteral("Add Folder"), content);
        auto* scanFolderButton =
            new QPushButton(QStringLiteral("Scan Selected"), content);
        auto* removeFolderButton =
            new QPushButton(QStringLiteral("Remove Path"), content);
        for (auto* button : {importFilesButton, addFolderButton,
                             scanFolderButton, removeFolderButton}) {
            button->setStyleSheet(
                "QPushButton { background: #252834; color: #E0E4F0; "
                "border: 1px solid #343846; border-radius: 0; padding: 7px 10px; }"
                "QPushButton:hover { background: #343A4B; }"
                "QPushButton:disabled { color: #666B78; }");
        }
        pathActionRow->addWidget(importFilesButton);
        pathActionRow->addWidget(addFolderButton);
        pathActionRow->addWidget(scanFolderButton);
        pathActionRow->addWidget(removeFolderButton);
        layout->addWidget(pathList);
        layout->addLayout(pathActionRow);
        layout->addWidget(pathStatus);

        auto refreshPathList = [this, pathList](const QString& preferred = {}) {
            const QSignalBlocker blocker(pathList);
            pathList->clear();
            int preferredRow = -1;
            const auto directories = m_libraryRepository.directories();
            for (int index = 0;
                 index < static_cast<int>(directories.size()); ++index) {
                const auto& directory =
                    directories[static_cast<std::size_t>(index)];
                const QString lastScanned = directory.lastScannedAt.isValid()
                    ? directory.lastScannedAt.toLocalTime().toString(
                          QStringLiteral("yyyy-MM-dd hh:mm"))
                    : QStringLiteral("Not scanned");
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1\nLast scan: %2")
                        .arg(directory.path, lastScanned),
                    pathList);
                item->setData(Qt::UserRole, directory.path);
                item->setData(Qt::UserRole + 1, directory.id);
                item->setToolTip(directory.path);
                if (directory.path == preferred) {
                    preferredRow = index;
                }
            }
            if (preferredRow >= 0) {
                pathList->setCurrentRow(preferredRow);
            } else if (pathList->count() > 0) {
                pathList->setCurrentRow(0);
            }
        };
        refreshPathList();

        connect(importFilesButton, &QPushButton::clicked, &dialog,
                [this, &dialog, pathStatus]() {
            const QStringList files = QFileDialog::getOpenFileNames(
                &dialog, QStringLiteral("Select Audio Files"), {},
                QStringLiteral("Audio files (*.mp3 *.flac *.wav)"));
            if (files.isEmpty()) {
                return;
            }
            if (m_libraryScanner->isRunning()) {
                pathStatus->setText(QStringLiteral("A scan is already running"));
                return;
            }
            pathStatus->setText(QStringLiteral("Importing audio files..."));
            m_libraryPage->setScanStatus(QStringLiteral("Importing audio files..."));
            m_libraryScanner->scanFiles(files);
        });
        connect(addFolderButton, &QPushButton::clicked, &dialog,
                [this, &dialog, pathStatus, refreshPathList]() {
            if (m_libraryScanner->isRunning()) {
                pathStatus->setText(QStringLiteral("Paths cannot be changed during a scan"));
                return;
            }
            const QString path = QFileDialog::getExistingDirectory(
                &dialog, QStringLiteral("Select Music Folder"));
            if (path.isEmpty()) {
                return;
            }
            const QString absolutePath = QDir(path).absolutePath();
            if (!m_libraryRepository.addDirectory(absolutePath)) {
                pathStatus->setText(m_libraryRepository.lastError());
                return;
            }
            refreshPathList(absolutePath);
            pathStatus->setText(QStringLiteral("Folder path added"));
        });
        connect(removeFolderButton, &QPushButton::clicked, &dialog,
                [this, pathList, pathStatus, refreshPathList]() {
            const auto* item = pathList->currentItem();
            if (!item) {
                pathStatus->setText(QStringLiteral("Select a folder path first"));
                return;
            }
            if (m_libraryScanner->isRunning()) {
                pathStatus->setText(QStringLiteral("Paths cannot be removed during a scan"));
                return;
            }
            const qint64 id = item->data(Qt::UserRole + 1).toLongLong();
            if (!m_libraryRepository.removeDirectory(id)) {
                pathStatus->setText(m_libraryRepository.lastError());
                return;
            }
            refreshPathList();
            pathStatus->setText(
                QStringLiteral("Path removed; imported tracks remain in the library"));
        });
        connect(scanFolderButton, &QPushButton::clicked, &dialog,
                [this, pathList, pathStatus]() {
            const auto* item = pathList->currentItem();
            if (!item) {
                pathStatus->setText(QStringLiteral("Add and select a folder path first"));
                return;
            }
            if (m_libraryScanner->isRunning()) {
                pathStatus->setText(QStringLiteral("A scan is already running"));
                return;
            }
            const QString path = item->data(Qt::UserRole).toString();
            pathStatus->setText(QStringLiteral("Scanning folder..."));
            m_libraryPage->setScanStatus(QStringLiteral("Scanning music folder..."));
            m_libraryScanner->scanDirectory(path);
        });
        connect(m_libraryScanner,
                &isac::library::LibraryScanner::scanFinished, &dialog,
                [pathStatus, refreshPathList](bool cancelled,
                                              const QString& error) {
            refreshPathList();
            if (!error.isEmpty()) {
                pathStatus->setText(
                    QStringLiteral("Scan failed: %1").arg(error));
            } else if (cancelled) {
                pathStatus->setText(QStringLiteral("Scan cancelled"));
            } else {
                pathStatus->setText(QStringLiteral("Scan complete"));
            }
        });

        addSectionTitle(QStringLiteral("Playback"));
        auto* playbackForm = new QFormLayout();
        auto* fadeEnabled = new QCheckBox("Enable fade in/out", &dialog);
        fadeEnabled->setChecked(
            settings.value(QStringLiteral("playback/fadeEnabled"), false).toBool());
        auto* fadeDuration = new QDoubleSpinBox(&dialog);
        fadeDuration->setRange(0.25, 10.0);
        fadeDuration->setSingleStep(0.25);
        fadeDuration->setSuffix(" s");
        fadeDuration->setDecimals(2);
        fadeDuration->setValue(
            settings.value(QStringLiteral("playback/fadeDurationSeconds"), 2.0)
                .toDouble());
        fadeDuration->setEnabled(fadeEnabled->isChecked());
        connect(fadeEnabled, &QCheckBox::toggled,
                fadeDuration, &QDoubleSpinBox::setEnabled);
        playbackForm->addRow(fadeEnabled);
        playbackForm->addRow("Fade duration", fadeDuration);
        auto* overlapEnabled = new QCheckBox(
            QStringLiteral("Enable track overlap"), &dialog);
        overlapEnabled->setChecked(settings.value(
            QStringLiteral("playback/overlapEnabled"), false).toBool());
        auto* overlapDuration = new QDoubleSpinBox(&dialog);
        overlapDuration->setRange(0.10, 10.0);
        overlapDuration->setSingleStep(0.25);
        overlapDuration->setSuffix(" s");
        overlapDuration->setDecimals(2);
        overlapDuration->setValue(std::clamp(
            settings.value(QStringLiteral("playback/overlapDurationSeconds"), 2.0)
                .toDouble(),
            0.10, 10.0));
        overlapDuration->setEnabled(overlapEnabled->isChecked());
        connect(overlapEnabled, &QCheckBox::toggled,
                overlapDuration, &QDoubleSpinBox::setEnabled);
        playbackForm->addRow(overlapEnabled);
        playbackForm->addRow("Overlap duration", overlapDuration);
        auto* gainMaximum = new QDoubleSpinBox(&dialog);
        gainMaximum->setRange(1.0, 4.0);
        gainMaximum->setSingleStep(0.1);
        gainMaximum->setDecimals(2);
        gainMaximum->setSuffix(QStringLiteral(" x"));
        gainMaximum->setValue(std::clamp(
            settings.value(QStringLiteral("playback/gainMaximum"), 2.0)
                .toDouble(),
            1.0, 4.0));
        gainMaximum->setToolTip(
            QStringLiteral("Maximum Gain for the volume slider; range: 1.00x to 4.00x"));
        playbackForm->addRow(QStringLiteral("Maximum Gain"), gainMaximum);
        layout->addLayout(playbackForm);

        addSectionTitle(QStringLiteral("Playback History"));
        auto* historyForm = new QFormLayout();
        auto* historyLimit = new QSpinBox(&dialog);
        historyLimit->setRange(5, 10000);
        historyLimit->setSingleStep(5);
        historyLimit->setSuffix(QStringLiteral(" tracks"));
        historyLimit->setValue(std::clamp(
            settings.value(QStringLiteral("playback/historyLimit"), 100)
                .toInt(),
            5, 10000));
        historyForm->addRow(QStringLiteral("Keep recent plays"), historyLimit);
        auto* onlyLatestPerTrack = new QCheckBox(
            QStringLiteral("Keep only the latest play for each track"), &dialog);
        onlyLatestPerTrack->setChecked(settings.value(
            QStringLiteral("playback/historyOnlyLatestPerTrack"), false)
                                           .toBool());
        historyForm->addRow(onlyLatestPerTrack);
        layout->addLayout(historyForm);

        addSectionTitle(QStringLiteral("Track Information"));
        auto* infoForm = new QFormLayout();
        auto makeInfoOption = [&settings, &dialog](const QString& label,
                                                   const QString& key) {
            auto* option = new QCheckBox(&dialog);
            option->setChecked(settings.value(key, true).toBool());
            return std::pair{label, option};
        };
        const auto [formatLabel, showFormat] =
            makeInfoOption("File Format", QStringLiteral("display/audioFormat"));
        const auto [sampleRateLabel, showSampleRate] =
            makeInfoOption("Sample Rate", QStringLiteral("display/sampleRate"));
        const auto [bitDepthLabel, showBitDepth] =
            makeInfoOption("Bit Depth", QStringLiteral("display/bitDepth"));
        const auto [channelsLabel, showChannels] =
            makeInfoOption("Channels", QStringLiteral("display/channels"));
        const auto [bitrateLabel, showBitrate] =
            makeInfoOption("Bitrate", QStringLiteral("display/bitrate"));
        infoForm->addRow(formatLabel, showFormat);
        infoForm->addRow(sampleRateLabel, showSampleRate);
        infoForm->addRow(bitDepthLabel, showBitDepth);
        infoForm->addRow(channelsLabel, showChannels);
        infoForm->addRow(bitrateLabel, showBitrate);
        layout->addLayout(infoForm);

        addSectionTitle(QStringLiteral("Visualization"));
        auto* visualizationForm = new QFormLayout();
        auto* fftSize = new QComboBox(&dialog);
        fftSize->addItem(QStringLiteral("2048 points"), 2048);
        fftSize->addItem(QStringLiteral("4096 points"), 4096);
        fftSize->addItem(QStringLiteral("8192 points"), 8192);
        fftSize->addItem(QStringLiteral("16384 points"), 16384);
        const int savedFftSize = normalizedVisualizationFftSize(
            settings.value(QStringLiteral("visualization/fftSize"), 8192)
                .toInt());
        const int fftIndex = fftSize->findData(savedFftSize);
        fftSize->setCurrentIndex(fftIndex >= 0 ? fftIndex : 2);
        fftSize->setToolTip(
            QStringLiteral("Larger FFT sizes improve frequency resolution and use more CPU"));
        visualizationForm->addRow(QStringLiteral("FFT size"), fftSize);

        auto* updateInterval = new QSpinBox(&dialog);
        updateInterval->setRange(16, 200);
        updateInterval->setSingleStep(1);
        updateInterval->setSuffix(QStringLiteral(" ms"));
        updateInterval->setValue(normalizedVisualizationUpdateInterval(
            settings.value(QStringLiteral("visualization/updateIntervalMs"), 33)
                .toInt()));
        updateInterval->setToolTip(
            QStringLiteral("Visualization refresh interval; it does not affect audio playback"));
        visualizationForm->addRow(QStringLiteral("Update interval"), updateInterval);
        layout->addLayout(visualizationForm);

        layout->addStretch();
        scrollArea->setWidget(content);
        dialogLayout->addWidget(scrollArea, 1);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialogLayout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        m_themeManager.setMode(static_cast<isac::ui::ThemeMode>(
            themeSelector->currentData().toInt()));
        settings.setValue(QStringLiteral("playback/fadeEnabled"),
                          fadeEnabled->isChecked());
        settings.setValue(QStringLiteral("playback/fadeDurationSeconds"),
                          fadeDuration->value());
        settings.setValue(QStringLiteral("playback/overlapEnabled"),
                          overlapEnabled->isChecked());
        settings.setValue(QStringLiteral("playback/overlapDurationSeconds"),
                          overlapDuration->value());
        settings.setValue(QStringLiteral("playback/gainMaximum"),
                          gainMaximum->value());
        settings.setValue(QStringLiteral("playback/historyLimit"),
                          historyLimit->value());
        settings.setValue(
            QStringLiteral("playback/historyOnlyLatestPerTrack"),
            onlyLatestPerTrack->isChecked());
        if (!m_libraryRepository.trimPlaybackHistory(historyLimit->value())) {
            m_libraryPage->setScanStatus(
                QStringLiteral("Failed to update playback history limit: %1")
                    .arg(m_libraryRepository.lastError()));
        } else {
            m_libraryPage->refreshPlaybackHistory();
        }
        m_audioEngine.setFadeDurationSeconds(fadeDuration->value());
        m_audioEngine.setFadeEnabled(fadeEnabled->isChecked());
        m_audioEngine.setOverlapDurationSeconds(overlapDuration->value());
        m_audioEngine.setOverlapEnabled(overlapEnabled->isChecked());
        m_nowPlayingHeader->setVolumeMaximum(gainMaximum->value());
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
        const int selectedFftSize = normalizedVisualizationFftSize(
            fftSize->currentData().toInt());
        const int selectedUpdateInterval =
            normalizedVisualizationUpdateInterval(updateInterval->value());
        settings.setValue(QStringLiteral("visualization/fftSize"),
                          selectedFftSize);
        settings.setValue(QStringLiteral("visualization/updateIntervalMs"),
                          selectedUpdateInterval);
        m_nowPlayingHeader->setWaterfallFftSize(selectedFftSize);
        if (m_visualizationTimer) {
            m_visualizationTimer->setInterval(selectedUpdateInterval);
        }
    });

    auto* fileMenu = ui->menubar->addMenu(QStringLiteral("File"));
    QAction* quitAction = fileMenu->addAction(QStringLiteral("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = ui->menubar->addMenu(QStringLiteral("Edit"));
    QAction* selectAllAction =
        editMenu->addAction(QStringLiteral("Select All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, []() {
        if (QWidget* focused = QApplication::focusWidget()) {
            QMetaObject::invokeMethod(
                focused, "selectAll", Qt::DirectConnection);
        }
    });
    QAction* findAction = editMenu->addAction(QStringLiteral("Find"));
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this]() {
        if (!m_stackedWidget) {
            return;
        }
        if (m_stackedWidget->currentWidget() == m_libraryPage) {
            m_libraryPage->toggleSearchInput();
        } else if (m_stackedWidget->currentWidget() == m_playlistPage) {
            m_playlistPage->toggleToolsPanel();
        }
    });

    auto* viewMenu = ui->menubar->addMenu(QStringLiteral("View"));
    auto* viewGroup = new QActionGroup(viewMenu);
    viewGroup->setExclusive(true);
    QAction* libraryAction = viewMenu->addAction(QStringLiteral("Library"));
    QAction* playlistsAction =
        viewMenu->addAction(QStringLiteral("Playlists"));
    QAction* routerAction = viewMenu->addAction(QStringLiteral("DSP Router"));
    for (QAction* action : {libraryAction, playlistsAction, routerAction}) {
        action->setCheckable(true);
        viewGroup->addAction(action);
    }
    libraryAction->setChecked(true);
    libraryAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    playlistsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    routerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(libraryAction, &QAction::triggered, btnNavLibrary,
            &QPushButton::click);
    connect(playlistsAction, &QAction::triggered, btnNavPlaylists,
            &QPushButton::click);
    connect(routerAction, &QAction::triggered, btnNavPatchbay,
            &QPushButton::click);
    connect(btnNavLibrary, &QPushButton::clicked, libraryAction,
            &QAction::setChecked);
    connect(btnNavPlaylists, &QPushButton::clicked, playlistsAction,
            &QAction::setChecked);
    connect(btnNavPatchbay, &QPushButton::clicked, routerAction,
            &QAction::setChecked);

    auto* settingsMenu = ui->menubar->addMenu(QStringLiteral("Settings"));
    QAction* preferencesAction =
        settingsMenu->addAction(QStringLiteral("Preferences..."));
    preferencesAction->setShortcut(QKeySequence::Preferences);
    connect(preferencesAction, &QAction::triggered, settingsButton,
            &QPushButton::click);

    // ==================== 2. 中央多页面切换容器 (QStackedWidget) ====================
    m_stackedWidget = new QStackedWidget(this);

    // 页面 0：ISACMusic 曲库界面
    m_libraryPage = new isac::ui::ISACMusicLibraryPage(&m_libraryRepository, this);
    m_libraryPage->refreshTracks();
    m_libraryScanner = new isac::library::LibraryScanner(
        libraryDatabasePath(),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("format_plugins")),
        this);
    connect(m_libraryScanner, &isac::library::LibraryScanner::batchCommitted,
            this, [this](int) { m_libraryPage->refreshTracks(); });
    connect(m_libraryScanner, &isac::library::LibraryScanner::progressChanged,
            this, [this](int visited) {
                m_libraryPage->setScanStatus(QString("Checked %1 tracks").arg(visited));
            });
    connect(m_libraryScanner, &isac::library::LibraryScanner::scanFinished,
            this, [this](bool cancelled, const QString& error) {
                if (!error.isEmpty()) {
                    m_libraryPage->setScanStatus("Scan failed: " + error);
                } else if (cancelled) {
                    m_libraryPage->setScanStatus("Scan cancelled");
                } else {
                    m_libraryPage->setScanStatus("Scan complete");
                }
                m_libraryPage->refreshTracks();
            });

    // 页面 1：Coppwr 风格 DSP 连线调音台页面
    m_playlistPage = new isac::ui::PlaylistPage(&m_libraryRepository, this);
    connect(m_playlistPage, &isac::ui::PlaylistPage::trackDoubleClicked,
            this, &MainWindow::playTrackById);
    QAction* playlistToolsAction =
        viewMenu->addAction(QStringLiteral("Playlist Tools"));
    playlistToolsAction->setCheckable(true);
    playlistToolsAction->setChecked(m_playlistPage->toolsPanelVisible());
    connect(playlistToolsAction, &QAction::toggled,
            m_playlistPage, &isac::ui::PlaylistPage::setToolsPanelVisible);
    connect(m_playlistPage,
            &isac::ui::PlaylistPage::toolsVisibilityChanged,
            playlistToolsAction, &QAction::setChecked);

    // 页面 2：Coppwr 风格 DSP 连线调音台页面
    auto* patchbayContainer = new QWidget(this);
    auto* patchLayout = new QVBoxLayout(patchbayContainer);
    patchLayout->setContentsMargins(0, 0, 0, 0);
    patchLayout->setSpacing(0);

    auto* patchControlBar = new QWidget(patchbayContainer);
    patchControlBar->setObjectName(QStringLiteral("dspRouterControlBar"));
    patchControlBar->setFixedHeight(58);
    patchControlBar->setStyleSheet(
        "QWidget#dspRouterControlBar { background: #191B22; "
        "border-bottom: 1px solid #30333D; }"
        "QLabel { color: #A5AAB8; padding: 0 8px; }"
        "QComboBox, QPushButton { min-height: 28px; max-height: 28px; "
        "background: #252834; color: #E0E4F0; "
        "border: 1px solid #343846; border-radius: 0; padding: 3px 9px; }"
        "QComboBox:hover, QPushButton:hover { background: #343A4B; "
        "border-color: #626875; }"
        "QComboBox:focus, QPushButton:focus { border-color: #FA2D48; }"
        "QPushButton:disabled { color: #666B78; background: #20232B; }"
        "QComboBox::drop-down { width: 22px; border: none; "
        "border-left: 1px solid #343846; }"
        "QComboBox QAbstractItemView { background: #20232B; "
        "color: #EAECEF; border: 1px solid #454A56; "
        "selection-background-color: #FA2D48; }");
    auto* patchHeader = new QHBoxLayout(patchControlBar);
    patchHeader->setContentsMargins(10, 8, 10, 14);
    patchHeader->setSpacing(6);

    auto* patchStatusBar = new QWidget(patchbayContainer);
    patchStatusBar->setObjectName(QStringLiteral("dspRouterStatusBar"));
    patchStatusBar->setFixedHeight(28);
    patchStatusBar->setStyleSheet(
        "QWidget#dspRouterStatusBar { background: transparent; "
        "border-top: 1px solid #30333D; }"
        "QLabel { color: #9499A6; font-size: 11px; padding: 0 10px; }");
    auto* patchStatusRow = new QHBoxLayout(patchStatusBar);
    patchStatusRow->setContentsMargins(0, 0, 0, 0);
    patchStatusRow->setSpacing(0);

    auto* presetSelector = new QComboBox(patchControlBar);
    presetSelector->setMinimumWidth(180);
    presetSelector->setToolTip("Select a DSP routing preset");
    auto* savePresetButton = new QPushButton("Save Preset", patchControlBar);
    auto* renamePresetButton = new QPushButton("Rename", patchControlBar);
    auto* deletePresetButton = new QPushButton("Delete Preset", patchControlBar);
    savePresetButton->setToolTip("Save the current canvas nodes and connections as a custom preset");
    renamePresetButton->setToolTip("Rename the selected custom preset");
    deletePresetButton->setToolTip("Delete the selected custom preset file");
    auto* clearConnectionsButton = new QPushButton("Clear Connections", patchControlBar);
    clearConnectionsButton->setToolTip("Remove all connections from the canvas");
    auto* lblPatchStatus = new QLabel(patchStatusBar);
    lblPatchStatus->setMinimumWidth(80);
    auto* outputSelector = new QComboBox(patchControlBar);
    outputSelector->setMinimumWidth(190);
    outputSelector->setToolTip("Select the audio output device");
    auto* refreshOutputsButton = new QPushButton("Refresh Devices", patchControlBar);
    refreshOutputsButton->setToolTip("Re-enumerate audio output devices");
    auto* outputStatus = new QLabel(patchStatusBar);
    patchHeader->addWidget(new QLabel("Preset", patchControlBar));
    patchHeader->addWidget(presetSelector);
    patchHeader->addWidget(savePresetButton);
    patchHeader->addWidget(renamePresetButton);
    patchHeader->addWidget(deletePresetButton);
    patchHeader->addWidget(clearConnectionsButton);
    patchHeader->addSpacing(18);
    patchHeader->addWidget(new QLabel("Audio Output", patchControlBar));
    patchHeader->addWidget(outputSelector);
    patchHeader->addWidget(refreshOutputsButton);
    patchHeader->addStretch();
    patchStatusRow->addWidget(outputStatus);
    patchStatusRow->addStretch();
    patchStatusRow->addWidget(lblPatchStatus);

    m_patchbayView = new isac::ui::PatchbayView(m_audioEngine, this);
    m_patchbayView->setStyleSheet(
        "border: 1px solid #2B2F3C; border-radius: 0;");

    // ==================== 3. 音频路由页内的 DSP 模块库 ====================
    auto* pluginPanel = new QWidget();
    pluginPanel->setStyleSheet(R"(
        QWidget { background: #191B22; color: #D0D4E0; }
        QListWidget {
            background: #14161C; border: 1px solid #2B2F3C;
            outline: none; padding: 4px;
        }
        QListWidget::item { padding: 9px 7px; border-radius: 0; }
        QListWidget::item:selected { background: #343A4B; color: white; }
        QPushButton {
            background: #252834; color: #E0E4F0; border: 1px solid #343846;
            border-radius: 0; padding: 8px; font-weight: bold;
        }
        QPushButton:hover { background: #FA2D48; border-color: #FA2D48; color: white; }
    )");

    auto* pluginLayout = new QVBoxLayout(pluginPanel);
    pluginLayout->setContentsMargins(10, 10, 10, 10);
    pluginLayout->setSpacing(8);

    auto* pluginList = new QListWidget(pluginPanel);
    auto* pluginDescription = new QLabel(pluginPanel);
    pluginDescription->setWordWrap(true);
    pluginDescription->setMinimumHeight(48);
    pluginDescription->setStyleSheet("color: #9499A6; padding: 3px;");
    auto* instantiateButton = new QPushButton("Instantiate on Canvas", pluginPanel);

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
        pluginDescription->setText(item ? item->toolTip() : "Select a DSP module");
    };
    updatePluginDescription();

    auto instantiateSelected = [this, pluginList, lblPatchStatus]() {
        const auto* item = pluginList->currentItem();
        if (!item) {
            return;
        }

        const auto node = m_pluginManager.createNode(item->data(Qt::UserRole).toString());
        if (!node) {
            lblPatchStatus->setText("Failed to instantiate plugin");
            return;
        }

        const isac::NodeId nodeId = m_audioEngine.addNode(node);
        if (!m_audioEngine.rebuildGraph()) {
            lblPatchStatus->setText("Plugin created, but graph compilation failed");
            return;
        }

        m_patchbayView->patchbayScene()->syncFromEngine();
        lblPatchStatus->setText(QString("Instantiated: %1 (Node %2)")
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

    m_pluginDock = new QDockWidget("DSP Module Library", this);
    m_pluginDock->setObjectName(QStringLiteral("dspPluginLibraryDock"));
    m_pluginDock->setAllowedAreas(Qt::RightDockWidgetArea);
    m_pluginDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);
    m_pluginDock->setMinimumWidth(240);
    m_pluginDock->setWidget(pluginPanel);
    m_pluginDock->setStyleSheet(R"(
        QDockWidget { background: #191B22; color: #E5E7EF; }
        QDockWidget::title {
            background: #252834; color: #E5E7EF;
            padding: 8px 10px; border-bottom: 1px solid #343846;
        }
    )");

    auto* inspectorPanel = new QWidget();
    inspectorPanel->setStyleSheet(
        "QWidget { background: #191B22; color: #D0D4E0; }");
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(10, 10, 10, 10);
    inspectorLayout->setSpacing(8);
    auto* inspector = new isac::ui::NodeInspectorWidget(inspectorPanel);
    inspectorLayout->addWidget(inspector, 1);

    m_inspectorDock = new QDockWidget("Node Inspector", this);
    m_inspectorDock->setObjectName(QStringLiteral("dspNodeInspectorDock"));
    m_inspectorDock->setAllowedAreas(Qt::RightDockWidgetArea);
    m_inspectorDock->setFeatures(QDockWidget::DockWidgetMovable |
                                 QDockWidget::DockWidgetFloatable |
                                 QDockWidget::DockWidgetClosable);
    m_inspectorDock->setMinimumWidth(240);
    m_inspectorDock->setWidget(inspectorPanel);
    m_inspectorDock->setStyleSheet(R"(
        QDockWidget { background: #191B22; color: #E5E7EF; }
        QDockWidget::title {
            background: #252834; color: #E5E7EF;
            padding: 8px 10px; border-bottom: 1px solid #343846;
        }
    )");

    m_routerWorkspace = new QMainWindow(patchbayContainer);
    m_routerWorkspace->setObjectName(QStringLiteral("dspRouterWorkspace"));
    m_routerWorkspace->setWindowFlags(Qt::Widget);
    m_routerWorkspace->setDockOptions(QMainWindow::AnimatedDocks |
                                      QMainWindow::AllowTabbedDocks);
    m_routerWorkspace->setCentralWidget(m_patchbayView);
    m_routerWorkspace->addDockWidget(Qt::RightDockWidgetArea, m_pluginDock);
    m_routerWorkspace->addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    m_routerWorkspace->splitDockWidget(m_pluginDock, m_inspectorDock,
                                       Qt::Vertical);
    m_routerWorkspace->resizeDocks(
        {m_pluginDock, m_inspectorDock}, {360, 260}, Qt::Vertical);
    m_pluginDock->hide();
    m_inspectorDock->hide();

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

    patchLayout->addWidget(patchControlBar);
    patchLayout->addWidget(m_routerWorkspace, 1);
    patchLayout->addWidget(patchStatusBar);

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
                label += QStringLiteral(" (System Default)");
            }
            outputSelector->addItem(label, id);
            outputSelector->setItemData(
                outputSelector->count() - 1,
                QStringLiteral("Maximum detected channels: %1; current output is stereo")
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
                    status = QStringLiteral("Unable to apply output device: %1")
                                 .arg(QString::fromStdString(error));
                    outputSelector->setCurrentIndex(0);
                    settings.setValue(
                        QStringLiteral("audio/outputDeviceId"), QByteArray{});
                }
            }
        }
        if (status.isEmpty()) {
            status = QStringLiteral("%1 devices | Engine request: %2 kHz")
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
            outputStatus->setText("Device is no longer available; refresh the list");
            return;
        }
        std::string error;
        if (!m_audioEngine.selectOutputDevice(*selected, error)) {
            refreshOutputDevices();
            outputStatus->setText(
                QStringLiteral("Switch failed; keeping the previous output: %1")
                    .arg(QString::fromStdString(error)));
            return;
        }
        QSettings settings;
        settings.setValue(QStringLiteral("audio/outputDeviceId"), id);
        outputStatus->setText(
            QStringLiteral("Current output: %1 (stereo, DSP request: %2 kHz)")
                .arg(QString::fromStdString(selected->name))
                .arg(m_audioEngine.outputSampleRate() / 1000.0, 0, 'g', 4));
    });

    upperLayout->addWidget(sidebar);
    upperLayout->addWidget(m_stackedWidget, 1);

    // ==================== 3. 顶部常驻播放工作区 ====================
    m_nowPlayingHeader = new isac::ui::NowPlayingHeader(this);
    m_nowPlayingHeader->setWaterfallFftSize(normalizedVisualizationFftSize(
        settings.value(QStringLiteral("visualization/fftSize"), 8192)
            .toInt()));
    m_nowPlayingHeader->setVolumeMaximum(std::clamp(
        settings.value(QStringLiteral("playback/gainMaximum"), 2.0).toDouble(),
        1.0, 4.0));
    m_playbackQueueView = new isac::ui::PlaybackQueueView(
        &m_libraryRepository, this);
    connect(m_playbackQueueView,
            &isac::ui::PlaybackQueueView::trackActivated, this,
            [this](qint64 trackId) {
        if (m_playbackQueue.selectTrack(trackId)) {
            persistPlaybackQueue(false);
            playQueuedTrack(trackId);
        }
    });
    connect(m_nowPlayingHeader,
            &isac::ui::NowPlayingHeader::queueViewRequested, this, [this]() {
        m_playbackQueueView->show();
        refreshPlaybackQueueView();
        m_playbackQueueView->raise();
        m_playbackQueueView->activateWindow();
    });
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
        setRouterDockWidgetsVisible(false);
        m_stackedWidget->setCurrentIndex(0);
    });
    connect(btnNavPlaylists, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPlaylists, btnNavPatchbay]() {
        btnNavLibrary->setChecked(false);
        btnNavPlaylists->setChecked(true);
        btnNavPatchbay->setChecked(false);
        setRouterDockWidgetsVisible(false);
        m_playlistPage->refresh();
        m_stackedWidget->setCurrentIndex(1);
    });
    connect(btnNavPatchbay, &QPushButton::clicked, this,
            [this, btnNavLibrary, btnNavPlaylists, btnNavPatchbay]() {
        btnNavLibrary->setChecked(false);
        btnNavPlaylists->setChecked(false);
        btnNavPatchbay->setChecked(true);
        setRouterDockWidgetsVisible(true);
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
                m_audioEngine.setMasterGain(volume);
            });
    connect(m_mprisController,
            &isac::platform::MprisController::playbackModeRequested,
            this, [this](int mode) {
                setPlaybackMode(
                    static_cast<isac::library::PlaybackMode>(mode));
            });
#endif

    // 双击表格歌曲播放
    connect(m_libraryPage, &isac::ui::ISACMusicLibraryPage::trackDoubleClicked,
            this, &MainWindow::playTrackById);
    connect(m_libraryPage,
            &isac::ui::ISACMusicLibraryPage::trackPlayNextRequested,
            this, &MainWindow::queueTrackNext);

    connect(m_nowPlayingHeader,
            &isac::ui::NowPlayingHeader::coverContextMenuRequested,
            this, [this](qint64 trackId, const QPoint& globalPosition) {
        const auto track = m_libraryRepository.trackById(trackId);
        if (!track) {
            return;
        }

        QMenu menu(this);
        menu.setStyleSheet(R"(
            QMenu {
                background: #20232B;
                color: #EAECEF;
                border: 1px solid #454A56;
                padding: 4px;
            }
            QMenu::item {
                color: #EAECEF;
                padding: 7px 34px 7px 12px;
                margin: 1px;
                background: transparent;
            }
            QMenu::item:selected {
                background: #FA2D48;
                color: #FFFFFF;
            }
            QMenu::item:disabled {
                color: #858B98;
            }
            QMenu::right-arrow {
                width: 10px;
                height: 10px;
                subcontrol-origin: padding;
                subcontrol-position: right center;
                right: 8px;
            }
        )");

        QAction* viewAlbum = menu.addAction(QStringLiteral("View Album"));
        connect(viewAlbum, &QAction::triggered, this,
                [this, album = track->album]() {
            m_navLibraryButton->click();
            m_libraryPage->showAlbum(
                album.trimmed().isEmpty()
                    ? QStringLiteral("Unknown Album") : album);
        });

        QMenu* artistMenu = menu.addMenu(QStringLiteral("View Artist"));
        QStringList artists = track->artist.split(
            QRegularExpression(QStringLiteral("\\s*/\\s*")),
            Qt::SkipEmptyParts);
        for (QString& artist : artists) {
            artist = artist.trimmed();
        }
        artists.removeAll(QString{});
        if (artists.isEmpty()) {
            artists.push_back(QStringLiteral("Unknown Artist"));
        }
        for (const QString& artist : artists) {
            QAction* action = artistMenu->addAction(artist);
            connect(action, &QAction::triggered, this,
                    [this, artist]() {
                m_navLibraryButton->click();
                m_libraryPage->showArtist(artist);
            });
        }

        QAction* playNextAction = menu.addAction(QStringLiteral("Play Next"));
        connect(playNextAction, &QAction::triggered, this,
                [this, trackId = track->id]() {
            queueTrackNext(trackId);
        });
        menu.addSeparator();
        QMenu* addMenu = menu.addMenu(QStringLiteral("Add to Playlist"));
        const auto playlists = m_libraryRepository.playlists();
        if (playlists.empty()) {
            QAction* emptyAction = addMenu->addAction(
                QStringLiteral("No playlists available"));
            emptyAction->setEnabled(false);
        } else {
            for (const auto& playlist : playlists) {
                QAction* action = addMenu->addAction(playlist.name);
                connect(action, &QAction::triggered, this,
                        [this, playlistId = playlist.id,
                         trackId = track->id]() {
                    const auto added = m_libraryRepository.addTracksToPlaylist(
                        playlistId, {trackId});
                    if (!added) {
                        m_libraryPage->setScanStatus(
                            QStringLiteral("Failed to add to playlist: %1")
                                .arg(m_libraryRepository.lastError()));
                        return;
                    }
                    m_playlistPage->refresh();
                    m_libraryPage->setScanStatus(
                        *added > 0
                            ? QStringLiteral("Added to playlist")
                            : QStringLiteral("Track is already in this playlist"));
                });
            }
        }
        menu.exec(globalPosition);
    });

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
                setPlaybackMode(
                    static_cast<isac::library::PlaybackMode>(mode));
            });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::seekRequested, this, [this](double sec) {
        m_audioEngine.seekToSeconds(sec);
#ifdef ISAC_HAS_MPRIS
        m_mprisController->notifySeeked(m_audioEngine.currentPositionSeconds());
#endif
    });
    connect(m_nowPlayingHeader, &isac::ui::NowPlayingHeader::volumeChanged, this, [this](float factor) {
        m_audioEngine.setMasterGain(factor);
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
        QStringLiteral("DSP preset directory: %1").arg(presetDirectory));
    QString presetError;
    if (!isac::ui::DspPresetCatalog::installBuiltIns(
            presetDirectory, &presetError)) {
        lblPatchStatus->setText(
            QStringLiteral("Failed to install built-in presets: %1").arg(presetError));
    }
    auto presets = std::make_shared<QVector<isac::ui::DspPreset>>();
    auto refreshPresetSelector =
        [presetSelector, presets, presetDirectory, lblPatchStatus](
            const QString& selectedId = QString{}) {
        QString loadError;
        *presets = isac::ui::DspPresetCatalog::load(
            presetDirectory, &loadError);
        const QSignalBlocker blocker(presetSelector);
        presetSelector->clear();
        presetSelector->addItem(QStringLiteral("Select Routing Preset"));
        int selectedIndex = 0;
        for (const auto& preset : *presets) {
            presetSelector->addItem(preset.name, preset.id);
            if (!selectedId.isEmpty() && preset.id == selectedId) {
                selectedIndex = presetSelector->count() - 1;
            }
        }
        presetSelector->setCurrentIndex(selectedIndex);
        if (!loadError.isEmpty()) {
            lblPatchStatus->setText(
                QStringLiteral("Some presets could not be loaded: %1").arg(loadError));
        }
    };
    refreshPresetSelector();

    auto selectedPreset = [presetSelector, presets]()
        -> const isac::ui::DspPreset* {
        const QString id = presetSelector->currentData().toString();
        const auto found = std::find_if(
            presets->cbegin(), presets->cend(),
            [&id](const isac::ui::DspPreset& preset) {
                return preset.id == id;
            });
        return found == presets->cend() ? nullptr : &*found;
    };
    auto updatePresetActions = [selectedPreset, renamePresetButton,
                                deletePresetButton]() {
        const auto* preset = selectedPreset();
        const bool editable = preset && !preset->builtIn;
        renamePresetButton->setEnabled(editable);
        deletePresetButton->setEnabled(editable);
    };
    updatePresetActions();
    connect(presetSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updatePresetActions](int) { updatePresetActions(); });

    auto currentRoleNodes = [this]() {
        QHash<QString, isac::NodeId> roles{
            {QStringLiteral("source"), m_audioEngine.sourceNodeId()},
            {QStringLiteral("sink"), m_audioEngine.sinkNodeId()}
        };
        const auto addLegacyRole =
            [&roles](const QString& role,
                     const std::shared_ptr<isac::IAudioNode>& node) {
            if (node) {
                roles.insert(role, node->id());
            }
        };
        addLegacyRole(QStringLiteral("gain"), m_audioEngine.gainNode());
        addLegacyRole(QStringLiteral("adder"), m_audioEngine.addNodePtr());
        addLegacyRole(QStringLiteral("eq"), m_audioEngine.equalizerNode());

        std::vector<std::pair<isac::NodeId,
                              std::shared_ptr<isac::IAudioNode>>> nodes;
        for (const auto& entry : m_audioEngine.nodes()) {
            if (entry.first != m_audioEngine.sourceNodeId() &&
                entry.first != m_audioEngine.sinkNodeId()) {
                nodes.push_back(entry);
            }
        }
        std::sort(nodes.begin(), nodes.end(),
                  [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        QHash<QString, int> instanceCounts;
        for (const auto& [id, node] : nodes) {
            const QString pluginId = m_pluginManager.pluginIdForNode(node);
            if (pluginId.isEmpty()) {
                continue;
            }
            if (pluginId == QStringLiteral("isac.gain") &&
                !roles.contains(QStringLiteral("gain"))) {
                roles.insert(QStringLiteral("gain"), id);
            } else if (pluginId == QStringLiteral("isac.add") &&
                       !roles.contains(QStringLiteral("adder"))) {
                roles.insert(QStringLiteral("adder"), id);
            } else if (pluginId == QStringLiteral("isac.biquad") &&
                       !roles.contains(QStringLiteral("eq"))) {
                roles.insert(QStringLiteral("eq"), id);
            }
            const int instance = instanceCounts.value(pluginId, 0);
            instanceCounts.insert(pluginId, instance + 1);
            roles.insert(QStringLiteral("%1#%2").arg(pluginId).arg(instance),
                         id);
        }
        return roles;
    };

    connect(presetSelector, QOverload<int>::of(&QComboBox::activated),
            this, [this, presetSelector, lblPatchStatus, presets,
                   currentRoleNodes]() {
        const QString selectedId = presetSelector->currentData().toString();
        const auto selectedIt = std::find_if(
            presets->cbegin(), presets->cend(),
            [&selectedId](const isac::ui::DspPreset& preset) {
                return preset.id == selectedId;
            });
        if (selectedIt == presets->cend()) {
            return;
        }
        const isac::ui::DspPreset preset = *selectedIt;
        auto roleNodes = currentRoleNodes();
        QVector<isac::NodeId> createdNodes;

        if (preset.version == 2) {
            for (const auto& nodeSpec : preset.nodes) {
                if (nodeSpec.pluginId.isEmpty() ||
                    roleNodes.contains(nodeSpec.role)) {
                    continue;
                }
                const auto node =
                    m_pluginManager.createNode(nodeSpec.pluginId);
                if (!node) {
                    for (const isac::NodeId id : createdNodes) {
                        m_audioEngine.removeNode(id);
                    }
                    lblPatchStatus->setText(
                        QStringLiteral("Preset requires an unavailable module: %1")
                            .arg(nodeSpec.pluginId));
                    return;
                }
                const isac::NodeId id = m_audioEngine.addNode(node);
                createdNodes.push_back(id);
                roleNodes.insert(nodeSpec.role, id);
            }
        } else {
            const QHash<QString, QString> legacyPluginIds{
                {QStringLiteral("gain"), QStringLiteral("isac.gain")},
                {QStringLiteral("adder"), QStringLiteral("isac.add")},
                {QStringLiteral("eq"), QStringLiteral("isac.biquad")}
            };
            QSet<QString> requiredRoles;
            for (const auto& connection : preset.connections) {
                requiredRoles.insert(connection.sourceRole);
                requiredRoles.insert(connection.destinationRole);
            }
            for (auto it = legacyPluginIds.cbegin();
                 it != legacyPluginIds.cend(); ++it) {
                if (!requiredRoles.contains(it.key()) ||
                    roleNodes.contains(it.key())) {
                    continue;
                }
                const auto node = m_pluginManager.createNode(it.value());
                if (!node) {
                    lblPatchStatus->setText(
                        QStringLiteral("Preset requires an unavailable module: %1")
                            .arg(it.value()));
                    return;
                }
                const isac::NodeId id = m_audioEngine.addNode(node);
                createdNodes.push_back(id);
                roleNodes.insert(it.key(), id);
            }
        }

        QString error;
        if (!isac::ui::DspPresetCatalog::apply(
                preset, m_audioEngine, roleNodes, &error)) {
            for (const isac::NodeId id : createdNodes) {
                m_audioEngine.removeNode(id);
            }
            if (!createdNodes.isEmpty()) {
                m_audioEngine.rebuildGraph();
            }
            lblPatchStatus->setText(
                QStringLiteral("Preset was not applied: %1").arg(error));
            return;
        }

        auto* scene = m_patchbayView->patchbayScene();
        QString appliedStatus =
            QStringLiteral("Loaded preset: %1").arg(preset.name);
        QSet<isac::NodeId> configuredNodes{
            m_audioEngine.sourceNodeId(),
            m_audioEngine.sinkNodeId()
        };
        if (preset.version == 2) {
            for (const auto& nodeSpec : preset.nodes) {
                const isac::NodeId id = roleNodes.value(nodeSpec.role);
                if (m_audioEngine.hasNode(id)) {
                    configuredNodes.insert(id);
                }
            }
        } else {
            for (const auto& connection : preset.connections) {
                for (const QString& role :
                     {connection.sourceRole, connection.destinationRole}) {
                    const isac::NodeId id = roleNodes.value(role);
                    if (m_audioEngine.hasNode(id)) {
                        configuredNodes.insert(id);
                    }
                }
            }
        }

        QVector<isac::NodeId> obsoleteNodes;
        for (const auto& entry : m_audioEngine.nodes()) {
            const isac::NodeId id = entry.first;
            if (!configuredNodes.contains(id)) {
                obsoleteNodes.push_back(id);
            }
        }
        for (const isac::NodeId id : obsoleteNodes) {
            m_audioEngine.removeNode(id);
        }
        if (!obsoleteNodes.isEmpty() && !m_audioEngine.rebuildGraph()) {
            appliedStatus =
                QStringLiteral("Preset applied, but unused node cleanup failed");
        }

        if (preset.version == 2) {
            QHash<isac::NodeId, QPointF> positions;
            for (const auto& nodeSpec : preset.nodes) {
                const isac::NodeId id = roleNodes.value(nodeSpec.role);
                positions.insert(id, nodeSpec.position);
            }
            scene->syncFromEngine();
            scene->applyNodePositions(positions);
        } else {
            scene->syncFromEngine();
        }
        lblPatchStatus->setText(appliedStatus);
    });

    connect(savePresetButton, &QPushButton::clicked, this,
            [this, presetDirectory, refreshPresetSelector, lblPatchStatus]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Save Routing Preset"),
            QStringLiteral("Preset name:"), QLineEdit::Normal, {}, &accepted);
        if (!accepted) {
            return;
        }

        isac::ui::DspPreset preset;
        preset.version = 2;
        preset.name = name.trimmed();
        auto* scene = m_patchbayView->patchbayScene();
        const auto positions = scene->nodePositions();
        QHash<isac::NodeId, QString> rolesByNode;
        rolesByNode.insert(m_audioEngine.sourceNodeId(),
                           QStringLiteral("source"));
        rolesByNode.insert(m_audioEngine.sinkNodeId(),
                           QStringLiteral("sink"));
        preset.nodes.push_back({
            QStringLiteral("source"), {},
            positions.value(m_audioEngine.sourceNodeId())
        });
        preset.nodes.push_back({
            QStringLiteral("sink"), {},
            positions.value(m_audioEngine.sinkNodeId())
        });

        std::vector<std::pair<isac::NodeId,
                              std::shared_ptr<isac::IAudioNode>>> nodes;
        for (const auto& entry : m_audioEngine.nodes()) {
            if (entry.first != m_audioEngine.sourceNodeId() &&
                entry.first != m_audioEngine.sinkNodeId()) {
                nodes.push_back(entry);
            }
        }
        std::sort(nodes.begin(), nodes.end(),
                  [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        QHash<QString, int> instanceCounts;
        for (const auto& [id, node] : nodes) {
            const QString pluginId = m_pluginManager.pluginIdForNode(node);
            if (pluginId.isEmpty()) {
                lblPatchStatus->setText(
                    QStringLiteral("Cannot save: the canvas contains an unknown node type"));
                return;
            }
            const int instance = instanceCounts.value(pluginId, 0);
            instanceCounts.insert(pluginId, instance + 1);
            const QString role =
                QStringLiteral("%1#%2").arg(pluginId).arg(instance);
            rolesByNode.insert(id, role);
            preset.nodes.push_back({
                role, pluginId, positions.value(id)
            });
        }
        for (const isac::Connection& connection :
             m_audioEngine.connections()) {
            if (!rolesByNode.contains(connection.srcNode) ||
                !rolesByNode.contains(connection.dstNode)) {
                lblPatchStatus->setText(
                    QStringLiteral("Cannot save: a connection references an unknown node"));
                return;
            }
            preset.connections.push_back({
                rolesByNode.value(connection.srcNode), connection.srcPort,
                rolesByNode.value(connection.dstNode), connection.dstPort
            });
        }

        QString error;
        if (!isac::ui::DspPresetCatalog::save(
                presetDirectory, preset, &error)) {
            lblPatchStatus->setText(
                QStringLiteral("Failed to save preset: %1").arg(error));
            return;
        }
        refreshPresetSelector(preset.id);
        lblPatchStatus->setText(
            QStringLiteral("Saved preset: %1").arg(preset.name));
    });

    connect(renamePresetButton, &QPushButton::clicked, this,
            [presetDirectory, presetSelector, presets,
             refreshPresetSelector, lblPatchStatus, this]() {
        const QString id = presetSelector->currentData().toString();
        const auto found = std::find_if(
            presets->cbegin(), presets->cend(),
            [&id](const isac::ui::DspPreset& preset) {
                return preset.id == id;
            });
        if (found == presets->cend() || found->builtIn) {
            return;
        }
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Rename Routing Preset"),
            QStringLiteral("Preset name:"), QLineEdit::Normal,
            found->name, &accepted);
        if (!accepted) {
            return;
        }
        QString error;
        if (!isac::ui::DspPresetCatalog::rename(
                presetDirectory, id, name, &error)) {
            lblPatchStatus->setText(
                QStringLiteral("Rename failed: %1").arg(error));
            return;
        }
        refreshPresetSelector(id);
        lblPatchStatus->setText(QStringLiteral("Preset renamed"));
    });

    connect(deletePresetButton, &QPushButton::clicked, this,
            [presetDirectory, presetSelector, presets,
             refreshPresetSelector, lblPatchStatus, this]() {
        const QString id = presetSelector->currentData().toString();
        const auto found = std::find_if(
            presets->cbegin(), presets->cend(),
            [&id](const isac::ui::DspPreset& preset) {
                return preset.id == id;
            });
        if (found == presets->cend() || found->builtIn) {
            return;
        }
        const QString name = found->name;
        if (QMessageBox::question(
                this, QStringLiteral("Delete Routing Preset"),
                QStringLiteral("Delete \"%1\"? This action cannot be undone.").arg(name),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!isac::ui::DspPresetCatalog::remove(
                presetDirectory, id, &error)) {
            lblPatchStatus->setText(
                QStringLiteral("Failed to delete preset: %1").arg(error));
            return;
        }
        refreshPresetSelector();
        lblPatchStatus->setText(
            QStringLiteral("Deleted preset: %1").arg(name));
    });

    connect(clearConnectionsButton, &QPushButton::clicked, this, [this, lblPatchStatus]() {
        m_audioEngine.clearConnections();
        if (!m_audioEngine.rebuildGraph()) {
            lblPatchStatus->setText("Graph compilation failed after clearing connections");
            return;
        }
        m_patchbayView->patchbayScene()->syncFromEngine();
        lblPatchStatus->setText("Connections cleared");
    });

    // 进度条刷新定时器 (每 200ms 同步一次底层解码器帧游标)
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this, outputStatus]() {
        m_audioEngine.servicePlaybackControl();
        const auto selectedOutput = m_audioEngine.selectedOutputDevice();
        const QString outputName = selectedOutput
            ? QString::fromStdString(selectedOutput->name)
            : QStringLiteral("System Default");
        outputStatus->setText(
            QStringLiteral("%1 | DSP request: %2 kHz")
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
            if (m_audioEngine.overlapEnabled() &&
                m_trackLoaded &&
                !m_audioEngine.isOverlapActive()) {
                if (m_preparedNextTrackId == 0) {
                    const auto next = m_playbackQueue.peekNext();
                    if (next) {
                        const auto nextTrack =
                            m_libraryRepository.trackById(*next);
                        const double overlap =
                            m_audioEngine.overlapDurationSeconds();
                        const double prepareAt =
                            std::max(0.0, duration - overlap - 0.5);
                        if (nextTrack && position >= prepareAt) {
                            std::string prepareError;
                            const QString playbackPath =
                                resolvePlaybackPath(*nextTrack, nullptr);
                            if (!playbackPath.isEmpty() &&
                                m_audioEngine.prepareNext(
                                    playbackPath.toStdString(),
                                    &prepareError)) {
                                m_preparedNextTrackId = *next;
                            }
                        }
                    }
                }
                if (m_preparedNextTrackId > 0 &&
                    m_audioEngine.hasPreparedNext() &&
                    position >= std::max(
                        0.0, duration -
                            m_audioEngine.overlapDurationSeconds())) {
                    m_audioEngine.startOverlap();
                }
            }
            if (m_audioEngine.consumeOverlapTransition() &&
                m_preparedNextTrackId > 0) {
                const qint64 nextTrackId = m_preparedNextTrackId;
                m_preparedNextTrackId = 0;
                m_playbackQueue.selectTrack(nextTrackId);
                m_currentTrackId = nextTrackId;
                m_currentTrackEndHandled = false;
                if (const auto nextTrack =
                        m_libraryRepository.trackById(nextTrackId)) {
                    auto displayTrack = *nextTrack;
                    isac::library::MetadataReader::readAudioProperties(
                        displayTrack);
                    m_nowPlayingHeader->setTrackInfo(displayTrack);
                    m_nowPlayingHeader->setPlayingState(true);
                    const QSettings playbackSettings;
                    const int historyLimit = std::clamp(
                        playbackSettings.value(
                            QStringLiteral("playback/historyLimit"), 100)
                            .toInt(),
                        5, 10000);
                    const bool onlyLatestPerTrack =
                        playbackSettings.value(
                            QStringLiteral(
                                "playback/historyOnlyLatestPerTrack"),
                            false)
                            .toBool();
                    m_libraryRepository.recordPlayback(
                        nextTrackId, historyLimit, onlyLatestPerTrack);
                    m_libraryPage->refreshPlaybackHistory();
                    const double nextPosition =
                        m_audioEngine.currentPositionSeconds();
                    m_nowPlayingHeader->updateProgress(
                        nextPosition, m_audioEngine.totalDurationSeconds());
#ifdef ISAC_HAS_MPRIS
                    m_mprisController->setTrack(*nextTrack);
                    m_mprisController->setPosition(nextPosition);
                    m_mprisController->setPlaybackState(true);
#endif
                    persistPlaybackQueue(false);
                }
            }
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
    m_visualizationTimer->start(normalizedVisualizationUpdateInterval(
        settings.value(QStringLiteral("visualization/updateIntervalMs"), 33)
            .toInt()));

    // Local widget stylesheets are kept as compact component styles. The
    // theme manager records those base styles and expands their color tokens
    // after the complete widget tree has been assembled.
    m_themeManager.applyToExistingWidgets();
}

void MainWindow::setRouterDockWidgetsVisible(bool visible)
{
    if (m_pluginDock) {
        m_pluginDock->setVisible(visible);
    }
    if (m_inspectorDock) {
        m_inspectorDock->setVisible(visible);
    }
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

void MainWindow::queueTrackNext(qint64 trackId)
{
    const auto track = m_libraryRepository.trackById(trackId);
    if (!track || !m_playbackQueue.insertNext(trackId)) {
        if (m_libraryPage) {
            m_libraryPage->setScanStatus(
                QStringLiteral("Unable to queue the selected track"));
        }
        return;
    }

    m_audioEngine.cancelPreparedNext();
    m_preparedNextTrackId = 0;
    persistPlaybackQueue(true);
    if (m_libraryPage) {
        m_libraryPage->setScanStatus(
            QStringLiteral("Queued next: %1").arg(track->title));
    }
}

void MainWindow::rebuildPlaybackQueue(qint64 currentTrackId) {
    std::vector<qint64> trackIds = currentTrackIds();
    m_playbackQueue.reset(std::move(trackIds), currentTrackId, m_playbackMode);
    m_playbackQueueNeedsSave = true;
    refreshPlaybackQueueView();
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

    m_audioEngine.cancelPreparedNext();
    m_preparedNextTrackId = 0;
    QString formatError;
    const QString playbackPath =
        resolvePlaybackPath(*track, &formatError);
    if (playbackPath.isEmpty()) {
        m_lastPlaybackError = formatError;
        m_trackLoaded = false;
        m_currentTrackEndHandled = false;
        m_nowPlayingHeader->setPlayingState(false);
        return false;
    }
    std::string playbackError;
    if (m_audioEngine.loadAndPlay(
            playbackPath.toStdString(), &playbackError)) {
        m_lastPlaybackError.clear();
        auto displayTrack = *track;
        displayTrack.playbackPath = playbackPath;
        if (track->playbackPath != playbackPath) {
            m_libraryRepository.updateTrackPlaybackPath(
                trackId, playbackPath);
        }
        isac::library::MetadataReader::readAudioProperties(displayTrack);
        m_currentTrackEndHandled = false;
        m_currentTrackId = trackId;
        m_trackLoaded = true;
        QSettings settings;
        const int historyLimit = std::clamp(
            settings.value(QStringLiteral("playback/historyLimit"), 100)
                .toInt(),
            5, 10000);
        const bool onlyLatestPerTrack = settings.value(
            QStringLiteral("playback/historyOnlyLatestPerTrack"), false)
                                            .toBool();
        if (!m_libraryRepository.recordPlayback(
                trackId, historyLimit, onlyLatestPerTrack)) {
            qWarning() << "Unable to record playback history:"
                       << m_libraryRepository.lastError();
        } else {
            m_libraryPage->refreshPlaybackHistory();
        }
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

QString MainWindow::resolvePlaybackPath(
    const isac::library::TrackInfo& track, QString* error)
{
    if (track.formatPlugin.isEmpty()) {
        return track.filePath;
    }
    if (!track.playbackPath.isEmpty() &&
        QFileInfo::exists(track.playbackPath)) {
        return track.playbackPath;
    }

    const auto metadata = m_formatPluginManager.inspect(track.filePath);
    if (!metadata.valid) {
        if (error) {
            *error = QStringLiteral("Unable to inspect %1").arg(track.filePath);
        }
        return {};
    }
    const QString extension = metadata.decodedFormat.isEmpty()
        ? QStringLiteral("flac")
        : metadata.decodedFormat;
    const QString target = isac::media::FormatCache::playbackPath(
        track.filePath, track.formatPlugin, extension);
    if (QFileInfo::exists(target)) {
        return target;
    }

    QString materializeError;
    if (!m_formatPluginManager.materialize(
            track.filePath, target, &materializeError)) {
        if (error) {
            *error = materializeError;
        }
        return {};
    }
    return target;
}

void MainWindow::persistPlaybackQueue(bool includeOrder) {
    if (includeOrder) {
        m_playbackQueueNeedsSave = !m_libraryRepository.savePlaybackQueue(
            m_playbackQueue.orderedTrackIds(),
            m_playbackQueue.sourceTrackIds(),
            m_playbackQueue.position(), m_playbackMode);
    } else {
        if (!m_libraryRepository.savePlaybackCursor(
                m_playbackQueue.position(), m_playbackMode)) {
            m_playbackQueueNeedsSave = !m_libraryRepository.savePlaybackQueue(
                m_playbackQueue.orderedTrackIds(),
                m_playbackQueue.sourceTrackIds(),
                m_playbackQueue.position(), m_playbackMode);
        }
    }
    refreshPlaybackQueueView();
}

void MainWindow::restorePlaybackQueue() {
    const auto saved = m_libraryRepository.loadPlaybackQueue();
    if (!saved) {
        return;
    }
    m_playbackMode = saved->mode;
    m_playbackQueue.restore(
        saved->trackIds, saved->sourceTrackIds,
        saved->position, saved->mode);
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
    refreshPlaybackQueueView();
}

void MainWindow::setPlaybackMode(isac::library::PlaybackMode mode)
{
    m_playbackMode = mode;
    m_audioEngine.cancelPreparedNext();
    m_preparedNextTrackId = 0;
    m_playbackQueue.setMode(mode);
    persistPlaybackQueue(true);
    if (m_playbackQueueView && !m_playbackQueueView->isVisible()) {
        refreshPlaybackQueueView(true);
    }
    m_nowPlayingHeader->setPlaybackMode(static_cast<int>(mode));
#ifdef ISAC_HAS_MPRIS
    m_mprisController->setPlaybackMode(mode);
#endif
}

void MainWindow::refreshPlaybackQueueView(bool force)
{
    if (m_playbackQueueView &&
        (force || m_playbackQueueView->isVisible())) {
        m_playbackQueueView->setQueue(
            m_playbackQueue.orderedTrackIds(),
            m_playbackQueue.currentTrackId().value_or(0));
    }
}

MainWindow::~MainWindow() {
    delete ui;
}
