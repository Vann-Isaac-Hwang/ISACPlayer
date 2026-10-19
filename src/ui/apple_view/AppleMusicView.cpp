#include "AppleMusicView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <utility>

namespace isac::ui {

// Helper：将任意 QPixmap 裁剪为精致圆角图片
static QPixmap makeRoundedPixmap(const QPixmap& src, int size, int radius) {
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap dest(size, size);
    dest.fill(Qt::transparent);
    QPainter p(&dest);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    return dest;
}

TrackTableModel::TrackTableModel(std::vector<library::TrackInfo>* tracks,
                                 const QIcon& defaultIcon,
                                 QObject* parent)
    : QAbstractTableModel(parent)
    , m_tracks(tracks)
    , m_defaultIcon(defaultIcon) {}

int TrackTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !m_tracks
        ? 0
        : static_cast<int>(m_tracks->size());
}

int TrackTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_tracks ||
        index.row() < 0 || index.row() >= static_cast<int>(m_tracks->size())) {
        return {};
    }

    const auto& track = (*m_tracks)[static_cast<std::size_t>(index.row())];
    if (role == Qt::DecorationRole && index.column() == 0) {
        if (const auto* cached = m_coverCache.object(index.row())) {
            return *cached;
        }

        const QPixmap cover = library::MetadataReader::loadCoverArt(track, 80);
        const QIcon icon = cover.isNull() ? m_defaultIcon : QIcon(cover);
        m_coverCache.insert(index.row(), new QIcon(icon));
        return icon;
    }
    if (role == Qt::ForegroundRole && (index.column() == 1 || index.column() == 2)) {
        return QColor("#A5AAB8");
    }
    if (role == Qt::ForegroundRole && index.column() == 3) {
        return QColor("#8A8F9E");
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case 0: return track.title;
    case 1: return track.artist;
    case 2: return track.album;
    case 3: {
        const int mins = track.durationSeconds / 60;
        const int secs = track.durationSeconds % 60;
        return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
    }
    default: return {};
    }
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0: return QStringLiteral("歌曲标题");
    case 1: return QStringLiteral("艺人");
    case 2: return QStringLiteral("专辑");
    case 3: return QStringLiteral("时长");
    default: return {};
    }
}

void TrackTableModel::appendTrack(library::TrackInfo track) {
    if (!m_tracks) {
        return;
    }
    const int row = static_cast<int>(m_tracks->size());
    beginInsertRows({}, row, row);
    m_tracks->push_back(std::move(track));
    endInsertRows();
}

// ==================== AppleLibraryPage ====================
AppleLibraryPage::AppleLibraryPage(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 16);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与导入按钮栏
    auto* topBar = new QHBoxLayout();
    auto* pageTitle = new QLabel("音乐资料库", this);
    pageTitle->setStyleSheet("font-size: 26px; font-weight: bold; color: #FFFFFF;");

    auto* btnAddFiles  = new QPushButton("+ 添加音乐文件", this);
    auto* btnAddFolder = new QPushButton("+ 扫描音乐文件夹", this);
    QString btnStyle = R"(
        QPushButton {
            background-color: #FA2D48; color: white; border: none;
            border-radius: 6px; padding: 7px 14px; font-weight: bold;
        }
        QPushButton:hover { background-color: #FF455E; }
    )";
    btnAddFiles->setStyleSheet(btnStyle);
    btnAddFolder->setStyleSheet(btnStyle);

    topBar->addWidget(pageTitle);
    topBar->addStretch();
    topBar->addWidget(btnAddFiles);
    topBar->addWidget(btnAddFolder);

    // 2. 顶部正在播放巨幅卡片 (Hero Banner)
    auto* heroCard = new QWidget(this);
    heroCard->setStyleSheet(R"(
        QWidget {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2A2438, stop:1 #1A1C24);
            border-radius: 14px;
        }
    )");
    auto* heroLayout = new QHBoxLayout(heroCard);
    heroLayout->setContentsMargins(20, 18, 20, 18);
    heroLayout->setSpacing(22);

    m_heroCover = new QLabel(this);
    m_heroCover->setFixedSize(110, 110);
    const QPixmap defaultHeroCover = library::MetadataReader::generateDefaultCover("ISAC", 220);
    m_heroCover->setPixmap(makeRoundedPixmap(
        defaultHeroCover, 110, 12));

    auto* heroTextLayout = new QVBoxLayout();
    auto* badge = new QLabel("NOW PLAYING WORKSTATION", this);
    badge->setStyleSheet("color: #FA2D48; font-size: 11px; font-weight: bold; background: transparent;");
    m_heroTitle = new QLabel("欢迎使用 ISACPlayer", this);
    m_heroTitle->setStyleSheet("color: white; font-size: 22px; font-weight: bold; background: transparent;");
    m_heroArtist = new QLabel("点击右上角导入 MP3 / FLAC 曲库，双击歌曲开始播放", this);
    m_heroArtist->setStyleSheet("color: #A0A5B5; font-size: 14px; background: transparent;");

    heroTextLayout->addStretch();
    heroTextLayout->addWidget(badge);
    heroTextLayout->addWidget(m_heroTitle);
    heroTextLayout->addWidget(m_heroArtist);
    heroTextLayout->addStretch();

    heroLayout->addWidget(m_heroCover);
    heroLayout->addLayout(heroTextLayout, 1);

    // 3. 歌曲列表表格 (Apple Music 极简无竖线风格)
    const QPixmap defaultRowCover = library::MetadataReader::generateDefaultCover("ISAC", 80);
    m_table = new QTableView(this);
    m_tableModel = new TrackTableModel(&m_tracks, QIcon(defaultRowCover), m_table);
    m_table->setModel(m_tableModel);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(52);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setIconSize(QSize(40, 40));
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 180);
    m_table->setColumnWidth(2, 200);
    m_table->setColumnWidth(3, 75);

    m_table->setStyleSheet(R"(
        QTableView {
            background-color: transparent; border: none; color: #EAEAEA; font-size: 13px;
            alternate-background-color: #1A1C22;
        }
        QHeaderView::section {
            background-color: transparent; color: #8A8F9E; font-weight: bold;
            border: none; border-bottom: 1px solid #2A2E39; padding: 6px; text-align: left;
        }
        QTableView::item {
            padding: 6px; border-bottom: 1px solid #1E2129;
        }
        QTableView::item:selected {
            background-color: #FA2D48; color: white; border-radius: 6px;
        }
    )");

    mainLayout->addLayout(topBar);
    mainLayout->addWidget(heroCard);
    mainLayout->addWidget(m_table, 1);

    connect(btnAddFiles,  &QPushButton::clicked, this, &AppleLibraryPage::requestImportFiles);
    connect(btnAddFolder, &QPushButton::clicked, this, &AppleLibraryPage::requestImportFolder);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit trackDoubleClicked(index.row());
        }
    });
}

void AppleLibraryPage::addTracks(const QStringList& filePaths) {
    for (const QString& path : filePaths) {
        const QString normalizedPath = QFileInfo(path).absoluteFilePath();
        if (m_knownPaths.contains(normalizedPath)) {
            continue;
        }
        m_knownPaths.insert(normalizedPath);

        m_tableModel->appendTrack(library::MetadataReader::readTrack(normalizedPath));
    }
}

void AppleLibraryPage::updateHeroBanner(const library::TrackInfo& track) {
    m_heroCover->setPixmap(makeRoundedPixmap(
        library::MetadataReader::loadCoverArt(track, 220), 110, 12));
    m_heroTitle->setText(track.title);
    m_heroArtist->setText(track.artist + " — " + track.album);
}

// ==================== NowPlayingBar ====================
NowPlayingBar::NowPlayingBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(84);
    setStyleSheet(R"(
        NowPlayingBar {
            background-color: #1E2028;
            border-top: 1px solid #2E323E;
        }
        QLabel { color: #EAEAEA; background: transparent; }
        QPushButton {
            background-color: #2B2F3C; color: white; border: none;
            border-radius: 18px; font-size: 14px; font-weight: bold;
        }
        QPushButton:hover { background-color: #FA2D48; }
        QSlider::groove:horizontal {
            height: 4px; background: #353946; border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #FA2D48; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #FFFFFF; width: 12px; height: 12px;
            margin: -4px 0; border-radius: 6px;
        }
    )");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 10, 24, 10);
    layout->setSpacing(20);

    // 1. 左侧：迷你封面 + 歌名/歌手
    m_coverThumb = new QLabel(this);
    m_coverThumb->setFixedSize(54, 54);
    const QPixmap defaultCover = library::MetadataReader::generateDefaultCover("ISAC", 108);
    m_coverThumb->setPixmap(makeRoundedPixmap(
        defaultCover, 54, 8));

    auto* metaLayout = new QVBoxLayout();
    m_titleLabel = new QLabel("未在播放", this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: white;");
    m_titleLabel->setFixedWidth(180);
    m_artistLabel = new QLabel("ISAC Audio Engine", this);
    m_artistLabel->setStyleSheet("font-size: 12px; color: #9499A6;");
    m_artistLabel->setFixedWidth(180);
    metaLayout->addStretch();
    metaLayout->addWidget(m_titleLabel);
    metaLayout->addWidget(m_artistLabel);
    metaLayout->addStretch();

    // 2. 中间：播放控制按钮 + 进度条
    auto* centerLayout = new QVBoxLayout();
    auto* btnRow = new QHBoxLayout();
    m_btnPrev      = new QPushButton("⏮", this);
    m_btnPlayPause = new QPushButton("▶", this);
    m_btnNext      = new QPushButton("⏭", this);
    m_btnPrev->setFixedSize(36, 36);
    m_btnPlayPause->setFixedSize(42, 42);
    m_btnPlayPause->setStyleSheet("background-color: #FA2D48; border-radius: 21px; font-size: 16px;");
    m_btnNext->setFixedSize(36, 36);

    btnRow->addStretch();
    btnRow->addWidget(m_btnPrev);
    btnRow->addSpacing(12);
    btnRow->addWidget(m_btnPlayPause);
    btnRow->addSpacing(12);
    btnRow->addWidget(m_btnNext);
    btnRow->addStretch();

    auto* seekRow = new QHBoxLayout();
    m_lblCurrentTime = new QLabel("0:00", this);
    m_lblCurrentTime->setStyleSheet("font-size: 11px; color: #8A8F9E;");
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setRange(0, 1000);
    m_lblTotalTime = new QLabel("0:00", this);
    m_lblTotalTime->setStyleSheet("font-size: 11px; color: #8A8F9E;");
    seekRow->addWidget(m_lblCurrentTime);
    seekRow->addWidget(m_seekSlider, 1);
    seekRow->addWidget(m_lblTotalTime);

    centerLayout->addLayout(btnRow);
    centerLayout->addLayout(seekRow);

    // 3. 右侧：DSP 乘法器实时音量推子
    auto* volLayout = new QHBoxLayout();
    auto* volIcon = new QLabel("🔊 DSP 增益", this);
    volIcon->setStyleSheet("font-size: 12px; color: #A0A5B5;");
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 200);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(130);
    volLayout->addWidget(volIcon);
    volLayout->addWidget(m_volumeSlider);

    layout->addWidget(m_coverThumb);
    layout->addLayout(metaLayout);
    layout->addLayout(centerLayout, 1);
    layout->addLayout(volLayout);

    connect(m_btnPlayPause, &QPushButton::clicked, this, &NowPlayingBar::playPauseClicked);
    connect(m_btnPrev,      &QPushButton::clicked, this, &NowPlayingBar::prevClicked);
    connect(m_btnNext,      &QPushButton::clicked, this, &NowPlayingBar::nextClicked);

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_isUserSeeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_isUserSeeking = false;
        if (m_totalDuration > 0.0) {
            double targetSec = (m_seekSlider->value() / 1000.0) * m_totalDuration;
            emit seekRequested(targetSec);
        }
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        emit volumeChanged(val / 100.0f);
    });
}

void NowPlayingBar::setTrackInfo(const library::TrackInfo& info) {
    m_coverThumb->setPixmap(makeRoundedPixmap(
        library::MetadataReader::loadCoverArt(info, 108), 54, 8));
    m_titleLabel->setText(info.title);
    m_artistLabel->setText(info.artist);
}

void NowPlayingBar::setPlayingState(bool playing) {
    m_btnPlayPause->setText(playing ? "⏸" : "▶");
}

void NowPlayingBar::updateProgress(double currentSec, double totalSec) {
    m_totalDuration = totalSec;
    m_lblCurrentTime->setText(formatTime(currentSec));
    m_lblTotalTime->setText(formatTime(totalSec));

    if (!m_isUserSeeking && totalSec > 0.0) {
        int pos = static_cast<int>((currentSec / totalSec) * 1000.0);
        m_seekSlider->setValue(std::clamp(pos, 0, 1000));
    }
}

QString NowPlayingBar::formatTime(double seconds) {
    int total = static_cast<int>(std::max(0.0, seconds));
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

} // namespace isac::ui
