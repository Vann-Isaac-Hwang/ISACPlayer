#include "AppleMusicView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QAction>
#include <QMenu>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QListView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QDataStream>
#include <QFontMetrics>
#include <QGridLayout>
#include <QResizeEvent>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolButton>
#include <algorithm>
#include <utility>

namespace {

class ResponsiveAlbumView final : public QListView {
public:
    using QListView::QListView;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QListView::resizeEvent(event);
        updateGrid();
    }

private:
    void updateGrid()
    {
        const int availableWidth = viewport()->width();
        if (availableWidth <= 0 || availableWidth == m_lastWidth) {
            return;
        }
        m_lastWidth = availableWidth;

        constexpr int targetCellWidth = 210;
        constexpr int gap = 14;
        const int columns = std::max(
            1, (availableWidth + gap) / (targetCellWidth + gap));
        const int cellWidth =
            (availableWidth - gap * (columns - 1)) / columns;
        const int coverSize = std::clamp(cellWidth - 24, 128, 196);
        setSpacing(gap);
        setIconSize(QSize(coverSize, coverSize));
        setGridSize(QSize(cellWidth, coverSize + 62));
    }

    int m_lastWidth = -1;
};

class AlbumCardDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option,
                         const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QString artist = index.data(Qt::UserRole).toString();
        if (!artist.isEmpty()) {
            option->text += QLatin1Char('\n') + artist;
        }
        option->features |= QStyleOptionViewItem::WrapText;
        option->displayAlignment = Qt::AlignHCenter;
    }
};

}

namespace isac::ui {

TrackTableModel::TrackTableModel(library::LibraryRepository* repository,
                                 const QIcon& defaultIcon,
                                 QObject* parent)
    : QAbstractTableModel(parent)
    , m_repository(repository)
    , m_defaultIcon(defaultIcon)
{
    refresh();
}

int TrackTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !m_repository
        ? 0 : static_cast<int>(std::min<qint64>(m_trackCount, INT_MAX));
}

int TrackTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    if (!loadPage(index.row())) {
        return {};
    }
    const auto& track = m_page[static_cast<std::size_t>(index.row() - m_pageStart)];
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

Qt::ItemFlags TrackTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (m_playlistId && m_searchText.isEmpty()) {
        itemFlags |= Qt::ItemIsDropEnabled;
        if (index.isValid()) {
            itemFlags |= Qt::ItemIsDragEnabled;
        }
    }
    return itemFlags;
}

QStringList TrackTableModel::mimeTypes() const {
    return {QStringLiteral("application/x-isac-playlist-track-id")};
}

QMimeData* TrackTableModel::mimeData(const QModelIndexList& indexes) const {
    if (!m_playlistId || !m_searchText.isEmpty() || indexes.isEmpty()) {
        return nullptr;
    }
    const int sourceRow = indexes.front().row();
    if (std::any_of(indexes.begin(), indexes.end(),
                    [sourceRow](const QModelIndex& index) {
                        return index.row() != sourceRow;
                    })) {
        return nullptr;
    }
    const auto track = trackAtRow(sourceRow);
    if (!track) {
        return nullptr;
    }
    auto* data = new QMimeData();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << track->id;
    data->setData(QStringLiteral("application/x-isac-playlist-track-id"),
                  payload);
    return data;
}

bool TrackTableModel::dropMimeData(const QMimeData* data,
                                   Qt::DropAction action, int row, int,
                                   const QModelIndex& parent) {
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (action != Qt::MoveAction || !m_playlistId ||
        !m_searchText.isEmpty() || !data ||
        !data->hasFormat(
            QStringLiteral("application/x-isac-playlist-track-id"))) {
        return false;
    }
    QByteArray payload = data->data(
        QStringLiteral("application/x-isac-playlist-track-id"));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    qint64 trackId = 0;
    stream >> trackId;
    if (stream.status() != QDataStream::Ok || trackId <= 0) {
        return false;
    }
    const int destination = row >= 0
        ? row : parent.isValid() ? parent.row() : rowCount();
    if (!m_repository->movePlaylistTrack(
            *m_playlistId, trackId, destination)) {
        return false;
    }
    refresh();
    return true;
}

Qt::DropActions TrackTableModel::supportedDropActions() const {
    return Qt::MoveAction;
}

void TrackTableModel::refresh() {
    beginResetModel();
    m_page.clear();
    m_pageStart = -1;
    m_coverCache.clear();
    if (!m_repository || (m_playlistContext && !m_playlistId)) {
        m_trackCount = 0;
    } else if (m_playlistContext) {
        m_trackCount = std::max<qint64>(
            0, m_repository->playlistTrackCount(*m_playlistId, m_searchText));
    } else if (!m_albumFilter.isEmpty()) {
        m_trackCount = m_repository->albumTrackCount(
            m_albumFilter, m_searchText);
    } else if (!m_artistFilter.isEmpty()) {
        m_trackCount = m_repository->artistTrackCount(
            m_artistFilter, m_searchText);
    } else {
        m_trackCount = m_repository->trackCount(m_searchText);
    }
    endResetModel();
}

void TrackTableModel::setSearchText(const QString& text) {
    const QString normalized = text.trimmed();
    if (m_searchText != normalized) {
        m_searchText = normalized;
        refresh();
    }
}

void TrackTableModel::setSort(library::TrackSort sort, bool ascending) {
    if (m_sort != sort || m_ascending != ascending) {
        m_sort = sort;
        m_ascending = ascending;
        refresh();
    }
}

void TrackTableModel::setPlaylistId(std::optional<qint64> playlistId) {
    if (!m_playlistContext || m_playlistId != playlistId) {
        m_playlistContext = true;
        m_playlistId = playlistId;
        m_artistFilter.clear();
        m_albumFilter.clear();
        refresh();
    }
}

void TrackTableModel::setCategoryFilter(const QString& artist,
                                        const QString& album) {
    if (m_artistFilter != artist || m_albumFilter != album ||
        m_playlistContext) {
        m_playlistContext = false;
        m_playlistId.reset();
        m_artistFilter = artist;
        m_albumFilter = album;
        refresh();
    }
}

bool TrackTableModel::loadPage(int row) const {
    if (!m_repository || row < 0 || row >= rowCount()) {
        return false;
    }
    const int pageStart = (row / kPageSize) * kPageSize;
    if (pageStart == m_pageStart) {
        return true;
    }
    if (m_playlistContext) {
        m_page = m_playlistId
            ? m_repository->queryPlaylistTracks(
                  *m_playlistId, m_searchText, kPageSize, pageStart)
            : std::vector<library::TrackInfo>{};
    } else if (!m_albumFilter.isEmpty()) {
        m_page = m_repository->queryAlbumTracks(
            m_albumFilter, m_searchText, m_sort, m_ascending,
            kPageSize, pageStart);
    } else if (!m_artistFilter.isEmpty()) {
        m_page = m_repository->queryArtistTracks(
            m_artistFilter, m_searchText, m_sort, m_ascending,
            kPageSize, pageStart);
    } else {
        m_page = m_repository->queryTracks(
            m_searchText, m_sort, m_ascending, kPageSize, pageStart);
    }
    m_pageStart = pageStart;
    return !m_page.empty();
}

std::optional<library::TrackInfo> TrackTableModel::trackAtRow(int row) const {
    if (!loadPage(row)) {
        return std::nullopt;
    }
    return m_page[static_cast<std::size_t>(row - m_pageStart)];
}

int TrackTableModel::rowForTrackId(qint64 id) const {
    if (!m_repository || id <= 0 ||
        !m_artistFilter.isEmpty() || !m_albumFilter.isEmpty()) {
        return -1;
    }
    const qint64 row = m_repository->rowForTrackId(
        id, m_sort, m_ascending, m_searchText);
    return row >= 0 && row < m_trackCount ? static_cast<int>(row) : -1;
}

std::vector<qint64> TrackTableModel::trackIds() const {
    std::vector<qint64> ids;
    ids.reserve(static_cast<std::size_t>(m_trackCount));
    constexpr int pageSize = 10000;
    for (qint64 offset = 0; offset < m_trackCount; offset += pageSize) {
        std::vector<qint64> page;
        if (m_playlistContext) {
            if (!m_playlistId) {
                break;
            }
            page = m_repository->playlistTrackIds(
                *m_playlistId, m_searchText, pageSize, static_cast<int>(offset));
        } else if (!m_albumFilter.isEmpty()) {
            page = m_repository->queryAlbumTrackIds(
                m_albumFilter, m_searchText, m_sort, m_ascending,
                pageSize, static_cast<int>(offset));
        } else if (!m_artistFilter.isEmpty()) {
            page = m_repository->queryArtistTrackIds(
                m_artistFilter, m_searchText, m_sort, m_ascending,
                pageSize, static_cast<int>(offset));
        } else {
            page = m_repository->queryTrackIds(
                m_sort, m_ascending, pageSize, static_cast<int>(offset),
                m_searchText);
        }
        if (page.empty()) {
            break;
        }
        ids.insert(ids.end(), page.begin(), page.end());
    }
    return ids;
}

qint64 TrackTableModel::trackCount() const {
    return m_trackCount;
}

std::optional<qint64> TrackTableModel::playlistId() const {
    return m_playlistId;
}

LibraryGroupTableModel::LibraryGroupTableModel(
    library::LibraryRepository* repository, bool albums, QObject* parent)
    : QAbstractTableModel(parent)
    , m_repository(repository)
    , m_albums(albums)
{
    refresh();
}

int LibraryGroupTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0
        : static_cast<int>(std::min<qint64>(m_groupCount, INT_MAX));
}

int LibraryGroupTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : (m_albums ? 3 : 2);
}

QVariant LibraryGroupTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !loadPage(index.row())) {
        return {};
    }
    const auto& group =
        m_page[static_cast<std::size_t>(index.row() - m_pageStart)];
    if (m_albums && index.column() == 0 &&
        (role == Qt::DecorationRole || role == Qt::ToolTipRole)) {
        if (role == Qt::ToolTipRole) {
            return QStringLiteral("%1\n%2 位曲目")
                .arg(group.artist)
                .arg(group.trackCount);
        }
        if (const auto* cached = m_coverCache.object(index.row())) {
            return *cached;
        }
        const auto tracks = m_repository->queryAlbumTracks(
            group.name, {}, library::TrackSort::Title, true, 1, 0);
        const QPixmap cover = tracks.empty()
            ? library::MetadataReader::generateDefaultCover(group.name, 180)
            : library::MetadataReader::loadCoverArt(tracks.front(), 180);
        const QIcon icon(cover.isNull()
            ? library::MetadataReader::generateDefaultCover(group.name, 180)
            : cover);
        m_coverCache.insert(index.row(), new QIcon(icon));
        return icon;
    }
    if (m_albums && index.column() == 0 && role == Qt::UserRole) {
        return group.artist;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    if (m_albums) {
        switch (index.column()) {
        case 0: return group.name;
        case 1: return group.artist;
        case 2: return group.trackCount;
        default: return {};
        }
    }
    switch (index.column()) {
    case 0: return group.name;
    case 1: return group.trackCount;
    default: return {};
    }
}

QVariant LibraryGroupTableModel::headerData(
    int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    if (m_albums) {
        switch (section) {
        case 0: return QStringLiteral("专辑");
        case 1: return QStringLiteral("艺人");
        case 2: return QStringLiteral("曲目数");
        default: return {};
        }
    }
    switch (section) {
    case 0: return QStringLiteral("艺人");
    case 1: return QStringLiteral("曲目数");
    default: return {};
    }
}

void LibraryGroupTableModel::refresh() {
    beginResetModel();
    m_page.clear();
    m_pageStart = -1;
    m_coverCache.clear();
    m_groupCount = !m_repository ? 0
        : m_albums ? m_repository->albumCount(m_searchText)
                   : m_repository->artistCount(m_searchText);
    endResetModel();
}

void LibraryGroupTableModel::setSearchText(const QString& text) {
    const QString normalized = text.trimmed();
    if (m_searchText != normalized) {
        m_searchText = normalized;
        refresh();
    }
}

void LibraryGroupTableModel::setAscending(bool ascending) {
    if (m_ascending != ascending) {
        m_ascending = ascending;
        refresh();
    }
}

bool LibraryGroupTableModel::loadPage(int row) const {
    if (!m_repository || row < 0 || row >= rowCount()) {
        return false;
    }
    const int pageStart = (row / kPageSize) * kPageSize;
    if (pageStart == m_pageStart) {
        return true;
    }
    m_page = m_albums
        ? m_repository->queryAlbums(
              m_searchText, m_ascending, kPageSize, pageStart)
        : m_repository->queryArtists(
              m_searchText, m_ascending, kPageSize, pageStart);
    m_pageStart = pageStart;
    return !m_page.empty();
}

std::optional<library::LibraryGroup>
LibraryGroupTableModel::groupAtRow(int row) const {
    if (!loadPage(row)) {
        return std::nullopt;
    }
    return m_page[static_cast<std::size_t>(row - m_pageStart)];
}

qint64 LibraryGroupTableModel::groupCount() const {
    return m_groupCount;
}

// ==================== AppleLibraryPage ====================
AppleLibraryPage::AppleLibraryPage(library::LibraryRepository* repository,
                                   QWidget* parent)
    : QWidget(parent)
    , m_repository(repository) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 16);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与导入按钮栏
    auto* topBar = new QHBoxLayout();
    auto* pageTitle = new QLabel("Library", this);
    pageTitle->setStyleSheet("font-size: 26px; font-weight: bold; color: #FFFFFF;");

    m_scanStatus = new QLabel(this);
    m_scanStatus->setStyleSheet("color: #9499A6; font-size: 12px;");

    topBar->addWidget(pageTitle);
    topBar->addStretch();
    topBar->addWidget(m_scanStatus);

    auto* libraryControls = new QHBoxLayout();
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("搜索歌曲、艺人或专辑");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setMinimumWidth(220);
    m_searchInput->setStyleSheet(
        "QLineEdit { background: #1E2028; color: #EAEAEA; border: 1px solid #343846; "
        "border-radius: 5px; padding: 7px 9px; }");
    m_sortSelector = new QComboBox(this);
    m_sortSelector->addItem("加入时间", static_cast<int>(library::TrackSort::AddedAt));
    m_sortSelector->addItem("标题", static_cast<int>(library::TrackSort::Title));
    m_sortSelector->addItem("艺人", static_cast<int>(library::TrackSort::Artist));
    m_sortSelector->addItem("专辑", static_cast<int>(library::TrackSort::Album));
    m_sortDirection = new QComboBox(this);
    m_sortDirection->addItem("升序", true);
    m_sortDirection->addItem("降序", false);
    for (QComboBox* selector : {m_sortSelector, m_sortDirection}) {
        selector->setStyleSheet(
            "QComboBox { background: #1E2028; color: #EAEAEA; border: 1px solid #343846; "
            "border-radius: 5px; padding: 6px 8px; }");
    }
    m_resultCount = new QLabel(this);
    m_resultCount->setStyleSheet("color: #9499A6; font-size: 12px;");
    libraryControls->addWidget(m_searchInput, 1);
    libraryControls->addWidget(m_sortSelector);
    libraryControls->addWidget(m_sortDirection);
    libraryControls->addWidget(m_resultCount);

    // 2. 歌曲列表表格 (Apple Music 极简无竖线风格)
    const QPixmap defaultRowCover = library::MetadataReader::generateDefaultCover("ISAC", 80);
    m_table = new QTableView(this);
    m_tableModel = new TrackTableModel(repository, QIcon(defaultRowCover), m_table);
    m_artistModel = new LibraryGroupTableModel(repository, false, m_table);
    m_albumModel = new LibraryGroupTableModel(repository, true, m_table);
    m_table->setModel(m_tableModel);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(52);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::InternalMove);
    m_table->setDefaultDropAction(Qt::MoveAction);
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
            background-color: #FA2D48; color: white; border-radius: 0;
        }
    )");

    m_libraryTabs = new QTabBar(this);
    m_libraryTabs->addTab(QStringLiteral("歌曲"));
    m_libraryTabs->addTab(QStringLiteral("艺人"));
    m_libraryTabs->addTab(QStringLiteral("专辑"));
    m_libraryTabs->setExpanding(false);
    m_libraryTabs->setStyleSheet(
        "QTabBar::tab { color: #A5AAB8; background: transparent; "
        "padding: 8px 18px; border-bottom: 2px solid transparent; }"
        "QTabBar::tab:selected { color: #FFFFFF; border-bottom-color: #FA2D48; }");
    m_groupBackButton = new QPushButton(QStringLiteral("返回分类"), this);
    m_groupBackButton->hide();

    m_albumViewMode = new QWidget(this);
    auto* albumViewModeLayout = new QHBoxLayout(m_albumViewMode);
    albumViewModeLayout->setContentsMargins(0, 0, 0, 0);
    albumViewModeLayout->setSpacing(0);
    m_albumListButton = new QToolButton(m_albumViewMode);
    m_albumCoverButton = new QToolButton(m_albumViewMode);
    m_albumListButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
    m_albumCoverButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_albumListButton->setToolTip(QStringLiteral("列表视图"));
    m_albumCoverButton->setToolTip(QStringLiteral("封面视图"));
    for (auto* button : {m_albumListButton, m_albumCoverButton}) {
        button->setCheckable(true);
        button->setFixedSize(34, 32);
        button->setStyleSheet(
            "QToolButton { background: #1E2028; color: #D0D4E0; "
            "border: 1px solid #343846; border-radius: 0; }"
            "QToolButton:checked { background: #393C45; color: white; }");
    }
    m_albumListButton->setChecked(true);
    albumViewModeLayout->addWidget(m_albumListButton);
    albumViewModeLayout->addWidget(m_albumCoverButton);
    m_albumViewMode->hide();

    m_albumCoverView = new ResponsiveAlbumView(this);
    m_albumCoverView->setModel(m_albumModel);
    m_albumCoverView->setItemDelegate(new AlbumCardDelegate(m_albumCoverView));
    m_albumCoverView->setModelColumn(0);
    m_albumCoverView->setViewMode(QListView::IconMode);
    m_albumCoverView->setResizeMode(QListView::Adjust);
    m_albumCoverView->setMovement(QListView::Static);
    m_albumCoverView->setWrapping(true);
    m_albumCoverView->setWordWrap(true);
    m_albumCoverView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_albumCoverView->setIconSize(QSize(164, 164));
    m_albumCoverView->setUniformItemSizes(true);
    m_albumCoverView->setStyleSheet(
        "QListView { background: transparent; border: none; color: #EAEAEA; "
        "font-size: 13px; outline: none; }"
        "QListView::item { padding: 7px; }"
        "QListView::item:selected { background: #343A4B; color: white; }");
    m_libraryContent = new QStackedWidget(this);
    m_libraryContent->addWidget(m_table);
    m_libraryContent->addWidget(m_albumCoverView);

    mainLayout->addLayout(topBar);
    mainLayout->addLayout(libraryControls);
    auto* viewBar = new QHBoxLayout();
    viewBar->addWidget(m_libraryTabs);
    viewBar->addWidget(m_groupBackButton);
    viewBar->addWidget(m_albumViewMode);
    viewBar->addStretch();
    mainLayout->addLayout(viewBar);
    mainLayout->addWidget(m_libraryContent, 1);

    connect(m_albumListButton, &QToolButton::clicked, this, [this]() {
        m_albumListButton->setChecked(true);
        m_albumCoverButton->setChecked(false);
        m_libraryContent->setCurrentWidget(m_table);
    });
    connect(m_albumCoverButton, &QToolButton::clicked, this, [this]() {
        m_albumListButton->setChecked(false);
        m_albumCoverButton->setChecked(true);
        m_libraryContent->setCurrentWidget(m_albumCoverView);
    });

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchInput, &QLineEdit::textChanged, m_searchTimer,
            QOverload<>::of(&QTimer::start));
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        m_tableModel->setSearchText(m_searchInput->text());
        m_artistModel->setSearchText(m_searchInput->text());
        m_albumModel->setSearchText(m_searchInput->text());
        updateResultCount();
    });
    auto updateSort = [this]() {
        m_tableModel->setSort(
            static_cast<library::TrackSort>(
                m_sortSelector->currentData().toInt()),
            m_sortDirection->currentData().toBool());
        const bool ascending = m_sortDirection->currentData().toBool();
        m_artistModel->setAscending(ascending);
        m_albumModel->setAscending(ascending);
        updateResultCount();
    };
    connect(m_sortSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateSort](int) { updateSort(); });
    connect(m_sortDirection, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateSort](int) { updateSort(); });
    connect(m_libraryTabs, &QTabBar::currentChanged,
            this, [this](int index) { showLibraryView(index); });
    connect(m_groupBackButton, &QPushButton::clicked, this, [this]() {
        m_showingGroupTracks = false;
        m_tableModel->setCategoryFilter({}, {});
        showLibraryView(m_libraryView);
    });
    updateResultCount();

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        if (m_libraryView == 0 || m_showingGroupTracks) {
            const auto track = m_tableModel->trackAtRow(index.row());
            if (track) {
                emit trackDoubleClicked(track->id);
            }
            return;
        }
        openGroupAtRow(index.row());
    });
    connect(m_albumCoverView, &QListView::doubleClicked, this,
            [this](const QModelIndex& index) {
        if (m_libraryView == 2 && !m_showingGroupTracks) {
            openGroupAtRow(index.row());
        }
    });

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this,
            [this](const QPoint& position) {
        if (m_libraryView != 0 || m_showingGroupTracks) {
            return;
        }
        const QModelIndex clickedIndex = m_table->indexAt(position);
        if (!clickedIndex.isValid()) {
            return;
        }
        if (!m_table->selectionModel()->isSelected(clickedIndex)) {
            m_table->selectRow(clickedIndex.row());
        }
        std::vector<qint64> selectedTrackIds;
        const QModelIndexList selectedRows =
            m_table->selectionModel()->selectedRows();
        selectedTrackIds.reserve(static_cast<std::size_t>(selectedRows.size()));
        for (const QModelIndex& selected : selectedRows) {
            if (const auto track = m_tableModel->trackAtRow(selected.row())) {
                selectedTrackIds.push_back(track->id);
            }
        }
        if (selectedTrackIds.empty()) {
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
        QMenu* addMenu = menu.addMenu(
            QStringLiteral("添加到歌单"));
        const auto playlists = m_repository->playlists();
        if (playlists.empty()) {
            QAction* emptyAction = addMenu->addAction(
                QStringLiteral("没有可用歌单"));
            emptyAction->setEnabled(false);
        } else {
            for (const auto& playlist : playlists) {
                QAction* action = addMenu->addAction(playlist.name);
                connect(action, &QAction::triggered, this,
                        [this, playlistId = playlist.id,
                         selectedTrackIds]() {
                    const auto added =
                        m_repository->addTracksToPlaylist(
                            playlistId, selectedTrackIds);
                    if (!added) {
                        m_scanStatus->setText(
                            QStringLiteral("添加到歌单失败：%1")
                                .arg(m_repository->lastError()));
                        return;
                    }
                    m_scanStatus->setText(
                        QStringLiteral("已添加 %1 首，跳过重复项 %2 首")
                            .arg(*added)
                            .arg(static_cast<qint64>(selectedTrackIds.size()) -
                                 *added));
                });
            }
        }
        menu.exec(m_table->viewport()->mapToGlobal(position));
    });
}

void AppleLibraryPage::refreshTracks() {
    m_tableModel->refresh();
    m_artistModel->refresh();
    m_albumModel->refresh();
    updateResultCount();
}

void AppleLibraryPage::setScanStatus(const QString& status) {
    m_scanStatus->setText(status);
}

std::optional<library::TrackInfo> AppleLibraryPage::trackAtRow(int row) const {
    return m_tableModel->trackAtRow(row);
}

int AppleLibraryPage::trackCount() const {
    return m_tableModel->rowCount();
}

std::vector<qint64> AppleLibraryPage::trackIds() const {
    return m_tableModel->trackIds();
}

int AppleLibraryPage::rowForTrackId(qint64 id) const {
    return m_tableModel->rowForTrackId(id);
}

void AppleLibraryPage::showLibraryView(int view) {
    m_libraryView = std::clamp(view, 0, 2);
    m_showingGroupTracks = false;
    m_groupBackButton->hide();
    if (m_libraryView == 0) {
        m_tableModel->setCategoryFilter({}, {});
        m_table->setModel(m_tableModel);
        m_libraryContent->setCurrentWidget(m_table);
    } else {
        m_tableModel->setCategoryFilter({}, {});
        m_table->setModel(m_libraryView == 1 ? m_artistModel : m_albumModel);
        m_libraryContent->setCurrentWidget(
            m_libraryView == 2 && m_albumCoverButton->isChecked()
                ? static_cast<QWidget*>(m_albumCoverView)
                : static_cast<QWidget*>(m_table));
    }
    m_albumViewMode->setVisible(m_libraryView == 2);
    if (m_libraryView != 2) {
        m_libraryContent->setCurrentWidget(m_table);
    }
    m_sortSelector->setVisible(m_libraryView == 0);
    m_sortSelector->setEnabled(m_libraryView == 0);
    m_sortDirection->setEnabled(true);
    updateResultCount();
}

void AppleLibraryPage::openGroupAtRow(int row) {
    const auto* model = m_libraryView == 1 ? m_artistModel : m_albumModel;
    const auto group = model->groupAtRow(row);
    if (group) {
        showGroupTracks(*group);
    }
}

void AppleLibraryPage::showGroupTracks(const library::LibraryGroup& group) {
    m_showingGroupTracks = true;
    const QString artist = m_libraryView == 1 ? group.name : QString{};
    const QString album = m_libraryView == 2 ? group.name : QString{};
    m_tableModel->setCategoryFilter(artist, album);
    m_table->setModel(m_tableModel);
    m_libraryContent->setCurrentWidget(m_table);
    m_albumViewMode->hide();
    m_groupBackButton->setText(
        QStringLiteral("返回%1").arg(m_libraryView == 1
            ? QStringLiteral("艺人") : QStringLiteral("专辑")));
    m_groupBackButton->show();
    m_sortSelector->show();
    m_sortSelector->setEnabled(true);
    m_sortDirection->setEnabled(true);
    updateResultCount();
}

void AppleLibraryPage::updateResultCount() {
    if (m_showingGroupTracks || m_libraryView == 0) {
        m_resultCount->setText(
            QStringLiteral("%1 首曲目").arg(m_tableModel->trackCount()));
    } else if (m_libraryView == 1) {
        m_resultCount->setText(
            QStringLiteral("%1 位艺人").arg(m_artistModel->groupCount()));
    } else {
        m_resultCount->setText(
            QStringLiteral("%1 张专辑").arg(m_albumModel->groupCount()));
    }
}

// ==================== PlaylistPage ====================
PlaylistPage::PlaylistPage(library::LibraryRepository* repository,
                           QWidget* parent)
    : QWidget(parent)
    , m_repository(repository)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 24, 32, 16);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Playlists"), this);
    title->setStyleSheet(
        "font-size: 26px; font-weight: bold; color: #FFFFFF;");
    root->addWidget(title);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    auto* playlistPanel = new QWidget(this);
    playlistPanel->setFixedWidth(220);
    playlistPanel->setStyleSheet(
        "QWidget { background: #191B22; }"
        "QListWidget { background: #14161C; color: #D0D4E0; "
        "border: 1px solid #2B2F3C; padding: 4px; }"
        "QListWidget::item { padding: 10px 8px; }"
        "QListWidget::item:selected { background: #343A4B; color: white; }");
    auto* playlistLayout = new QVBoxLayout(playlistPanel);
    playlistLayout->setContentsMargins(10, 10, 10, 10);
    playlistLayout->setSpacing(8);

    m_playlistList = new QListWidget(playlistPanel);
    auto* createButton = new QPushButton(QStringLiteral("新建歌单"), playlistPanel);
    m_renameButton = new QPushButton(QStringLiteral("重命名"), playlistPanel);
    m_deleteButton = new QPushButton(QStringLiteral("删除歌单"), playlistPanel);
    for (auto* button : {createButton, m_renameButton, m_deleteButton}) {
        button->setStyleSheet(
            "QPushButton { background: #252834; color: #E0E4F0; "
            "border: 1px solid #343846; border-radius: 5px; padding: 7px; }"
            "QPushButton:hover { background: #343A4B; }"
            "QPushButton:disabled { color: #666B78; }");
    }
    playlistLayout->addWidget(m_playlistList, 1);
    playlistLayout->addWidget(createButton);
    playlistLayout->addWidget(m_renameButton);
    playlistLayout->addWidget(m_deleteButton);
    body->addWidget(playlistPanel);

    auto* tracksPanel = new QWidget(this);
    auto* tracksLayout = new QVBoxLayout(tracksPanel);
    tracksLayout->setContentsMargins(0, 0, 0, 0);
    tracksLayout->setSpacing(8);
    auto* toolbar = new QHBoxLayout();
    m_searchInput = new QLineEdit(tracksPanel);
    m_searchInput->setPlaceholderText(QStringLiteral("筛选当前歌单"));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setStyleSheet(
        "QLineEdit { background: #1E2028; color: #EAEAEA; "
        "border: 1px solid #343846; border-radius: 5px; padding: 7px 9px; }");
    m_addTracksButton = new QPushButton(QStringLiteral("添加曲目"), tracksPanel);
    m_removeTracksButton = new QPushButton(QStringLiteral("从歌单移除"), tracksPanel);
    for (auto* button : {m_addTracksButton, m_removeTracksButton}) {
        button->setStyleSheet(
            "QPushButton { background: #252834; color: #E0E4F0; "
            "border: 1px solid #343846; border-radius: 5px; padding: 7px 10px; }"
            "QPushButton:hover { background: #343A4B; }"
            "QPushButton:disabled { color: #666B78; }");
    }
    toolbar->addWidget(m_searchInput, 1);
    toolbar->addWidget(m_addTracksButton);
    toolbar->addWidget(m_removeTracksButton);
    tracksLayout->addLayout(toolbar);

    const QPixmap defaultCover =
        library::MetadataReader::generateDefaultCover("ISAC", 80);
    m_table = new QTableView(tracksPanel);
    m_tableModel = new TrackTableModel(
        m_repository, QIcon(defaultCover), m_table);
    m_table->setModel(m_tableModel);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(52);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::InternalMove);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setIconSize(QSize(40, 40));
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 180);
    m_table->setColumnWidth(2, 200);
    m_table->setColumnWidth(3, 75);
    m_table->setStyleSheet(
        "QTableView { background: transparent; border: none; color: #EAEAEA; "
        "font-size: 13px; alternate-background-color: #1A1C22; }"
        "QHeaderView::section { background: transparent; color: #8A8F9E; "
        "font-weight: bold; border: none; border-bottom: 1px solid #2A2E39; "
        "padding: 6px; }"
        "QTableView::item { padding: 6px; border-bottom: 1px solid #1E2129; }"
        "QTableView::item:selected { background: #FA2D48; color: white; }");
    tracksLayout->addWidget(m_table, 1);
    body->addWidget(tracksPanel, 1);
    root->addLayout(body, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #9499A6; font-size: 12px;");
    root->addWidget(m_statusLabel);

    connect(createButton, &QPushButton::clicked, this, [this]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("新建歌单"), QStringLiteral("歌单名称"),
            QLineEdit::Normal, {}, &accepted);
        if (!accepted) {
            return;
        }
        const auto id = m_repository->createPlaylist(name);
        if (!id) {
            m_statusLabel->setText(m_repository->lastError());
            return;
        }
        refreshPlaylistList(*id);
    });
    connect(m_renameButton, &QPushButton::clicked, this, [this]() {
        const auto id = selectedPlaylistId();
        if (!id) {
            return;
        }
        const QString current =
            m_playlistList->currentItem()->data(Qt::UserRole + 1).toString();
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("重命名歌单"), QStringLiteral("歌单名称"),
            QLineEdit::Normal, current, &accepted);
        if (accepted && m_repository->renamePlaylist(*id, name)) {
            refreshPlaylistList(*id);
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const auto id = selectedPlaylistId();
        if (!id || QMessageBox::question(
                this, QStringLiteral("删除歌单"),
                QStringLiteral("删除歌单？曲库中的音乐文件不会被删除。"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        if (m_repository->deletePlaylist(*id)) {
            refreshPlaylistList();
        }
    });
    connect(m_playlistList, &QListWidget::currentRowChanged,
            this, [this](int) {
                const auto id = selectedPlaylistId();
                m_tableModel->setPlaylistId(id);
                m_searchInput->clear();
                updateControls();
            });
    connect(m_searchInput, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                m_tableModel->setSearchText(text);
                updateControls();
            });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() { updateControls(); });
    connect(m_addTracksButton, &QPushButton::clicked, this,
            [this]() { openAddTracksDialog(); });
    connect(m_removeTracksButton, &QPushButton::clicked, this, [this]() {
        const auto id = selectedPlaylistId();
        if (!id) {
            return;
        }
        int removed = 0;
        for (const auto& index : m_table->selectionModel()->selectedRows()) {
            const auto track = m_tableModel->trackAtRow(index.row());
            if (track && m_repository->removeTrackFromPlaylist(*id, track->id)) {
                ++removed;
            }
        }
        refreshPlaylistList(*id);
        m_statusLabel->setText(QStringLiteral("已移除 %1 首曲目").arg(removed));
    });
    connect(m_table, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) {
                const auto track = m_tableModel->trackAtRow(index.row());
                if (track) {
                    emit trackDoubleClicked(track->id);
                }
            });

    refresh();
}

void PlaylistPage::refresh()
{
    refreshPlaylistList(selectedPlaylistId());
}

std::vector<qint64> PlaylistPage::trackIds() const
{
    return m_tableModel->trackIds();
}

std::optional<qint64> PlaylistPage::selectedPlaylistId() const
{
    if (!m_playlistList || !m_playlistList->currentItem()) {
        return std::nullopt;
    }
    const qint64 id =
        m_playlistList->currentItem()->data(Qt::UserRole).toLongLong();
    return id > 0 ? std::optional<qint64>(id) : std::nullopt;
}

void PlaylistPage::refreshPlaylistList(std::optional<qint64> preferred)
{
    const qint64 preferredId = preferred.value_or(
        selectedPlaylistId().value_or(0));
    const QSignalBlocker blocker(m_playlistList);
    m_playlistList->clear();
    int selectedRow = -1;
    const auto playlists = m_repository->playlists();
    for (int index = 0; index < static_cast<int>(playlists.size()); ++index) {
        const auto& playlist = playlists[static_cast<std::size_t>(index)];
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(playlist.name).arg(playlist.trackCount),
            m_playlistList);
        item->setData(Qt::UserRole, playlist.id);
        item->setData(Qt::UserRole + 1, playlist.name);
        if (playlist.id == preferredId) {
            selectedRow = index;
        }
    }
    if (selectedRow < 0 && !playlists.empty()) {
        selectedRow = 0;
    }
    m_playlistList->setCurrentRow(selectedRow);
    const auto id = selectedPlaylistId();
    m_tableModel->setPlaylistId(id);
    m_tableModel->refresh();
    updateControls();
}

void PlaylistPage::updateControls()
{
    const bool hasPlaylist = selectedPlaylistId().has_value();
    const bool hasSelection = m_table->selectionModel() &&
        !m_table->selectionModel()->selectedRows().isEmpty();
    m_renameButton->setEnabled(hasPlaylist);
    m_deleteButton->setEnabled(hasPlaylist);
    m_addTracksButton->setEnabled(hasPlaylist);
    m_removeTracksButton->setEnabled(hasPlaylist && hasSelection);
}

void PlaylistPage::openAddTracksDialog()
{
    const auto playlistId = selectedPlaylistId();
    if (!playlistId) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("添加曲目到歌单"));
    dialog.resize(760, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("搜索歌曲、艺人或专辑"));
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setUniformItemSizes(true);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    auto populate = [this, list, playlistId](const QString& text) {
        list->clear();
        const auto tracks = m_repository->queryTracks(
            text, library::TrackSort::AddedAt, true, 1000, 0);
        for (const auto& track : tracks) {
            if (m_repository->playlistContainsTrack(*playlistId, track.id)) {
                continue;
            }
            auto* item = new QListWidgetItem(
                QStringLiteral("%1 — %2").arg(track.title, track.artist), list);
            item->setData(Qt::UserRole, track.id);
        }
    };
    connect(search, &QLineEdit::textChanged, &dialog, populate);
    populate({});
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    int added = 0;
    for (const auto* item : list->selectedItems()) {
        if (m_repository->addTrackToPlaylist(
                *playlistId, item->data(Qt::UserRole).toLongLong())) {
            ++added;
        }
    }
    refreshPlaylistList(*playlistId);
    m_statusLabel->setText(QStringLiteral("已添加 %1 首曲目").arg(added));
}

// ==================== NowPlayingHeader ====================
NowPlayingHeader::NowPlayingHeader(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("nowPlayingHeader"));
    setMinimumHeight(160);
    setMaximumHeight(180);
    setStyleSheet(R"(
        QWidget#nowPlayingHeader { background: #181A20; }
        QLabel { color: #EAEAEA; background: transparent; }
        QPushButton {
            color: #E6E8ED; background: #292C34; border: none;
            border-radius: 0; margin: 0; padding: 0; font-size: 16px;
        }
        QSlider::groove:vertical { width: 24px; background: #42454E; border: none; }
        QSlider::add-page:vertical,
        QSlider::sub-page:vertical { background: #42454E; border: none; }
        QSlider::handle:vertical {
            background: #E6E8ED; height: 12px; width: 24px; margin: 0;
            border: none; border-radius: 0;
        }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* contentWidget = new QWidget(this);
    contentWidget->setFixedHeight(140);
    auto* content = new QHBoxLayout(contentWidget);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(0);
    auto* controlGroup = new QWidget(this);
    controlGroup->setFixedSize(140, 140);
    controlGroup->setStyleSheet(QStringLiteral("background: #292C34;"));
    auto* controlGrid = new QGridLayout(controlGroup);
    controlGrid->setContentsMargins(0, 0, 0, 0);
    controlGrid->setHorizontalSpacing(0);
    controlGrid->setVerticalSpacing(0);
    m_playbackModeButton = new QPushButton(this);
    m_btnPrev = new QPushButton(this);
    m_btnPlayPause = new QPushButton(this);
    m_btnNext = new QPushButton(this);
    m_btnPrev->setIcon(QIcon(QStringLiteral(":/buttons/previous.png")));
    m_btnNext->setIcon(QIcon(QStringLiteral(":/buttons/next.png")));
    m_btnPlayPause->setIcon(QIcon(QStringLiteral(":/buttons/play.png")));
    for (auto* button : {m_btnPrev, m_btnPlayPause, m_btnNext,
                         m_playbackModeButton}) {
        button->setIconSize(QSize(31, 31));
    }
    m_btnPrev->setToolTip(QStringLiteral("上一首"));
    m_btnNext->setToolTip(QStringLiteral("下一首"));
    m_btnPlayPause->setToolTip(QStringLiteral("播放"));
    setPlaybackMode(0);
    const QList<QPushButton*> controls{
        m_playbackModeButton, m_btnPrev, m_btnPlayPause, m_btnNext};
    for (auto* button : controls) {
        button->setFixedSize(70, 70);
        button->setStyleSheet(R"(
            QPushButton {
                color: #E6E8ED; background: #292C34; border: none;
                border-radius: 0; margin: 0; padding: 0; font-size: 16px;
            }
            QPushButton:hover { background: #454952; }
            QPushButton:pressed { background: #565A64; }
        )");
    }
    controlGrid->addWidget(m_btnPlayPause, 0, 0);
    controlGrid->addWidget(m_playbackModeButton, 0, 1);
    controlGrid->addWidget(m_btnPrev, 1, 0);
    controlGrid->addWidget(m_btnNext, 1, 1);
    content->addWidget(controlGroup);

    m_volumeSlider = new QSlider(Qt::Vertical, this);
    m_volumeSlider->setRange(0, 200);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedSize(24, 140);
    m_volumeSlider->setToolTip(QStringLiteral("音量"));
    content->addWidget(m_volumeSlider);

    m_coverThumb = new QLabel(this);
    m_coverThumb->setFixedSize(140, 140);
    m_coverThumb->setScaledContents(true);
    m_coverThumb->setPixmap(library::MetadataReader::generateDefaultCover("ISAC", 280));
    content->addWidget(m_coverThumb);

    auto* visualColumn = new QVBoxLayout();
    visualColumn->setContentsMargins(0, 0, 0, 0);
    visualColumn->setSpacing(0);
    m_waveform = new WaveformProgressWidget(this);
    m_waveform->setMinimumHeight(36);
    auto* spectrumRow = new QHBoxLayout();
    spectrumRow->setContentsMargins(0, 0, 0, 0);
    spectrumRow->setSpacing(0);
    m_preDspWaterfall = new WaterfallWidget(this);
    m_postDspWaterfall = new WaterfallWidget(this);
    spectrumRow->addWidget(m_preDspWaterfall, 1);
    spectrumRow->addWidget(m_postDspWaterfall, 1);
    visualColumn->addWidget(m_waveform);
    visualColumn->addLayout(spectrumRow, 1);
    content->addLayout(visualColumn, 1);
    root->addWidget(contentWidget);

    auto* infoBar = new QWidget(this);
    infoBar->setObjectName(QStringLiteral("trackInfoBar"));
    infoBar->setStyleSheet(
        "QWidget#trackInfoBar { background: transparent; "
        "border-bottom: 1px solid #30333D; }");
    auto* infoRow = new QHBoxLayout(infoBar);
    infoRow->setContentsMargins(8, 0, 0, 2);
    infoRow->setSpacing(8);
    m_titleLabel = new QLabel(QStringLiteral("未在播放"), this);
    m_titleLabel->setStyleSheet("font-size: 13px; font-weight: bold;");
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_artistLabel = new QLabel(QStringLiteral("ISAC Audio Engine"), this);
    m_artistLabel->setStyleSheet("font-size: 11px; color: #A5A8B0;");
    m_artistLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_albumLabel = new QLabel(this);
    m_albumLabel->setStyleSheet("font-size: 11px; color: #A5A8B0;");
    m_albumLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_audioInfoLabel = new QLabel(this);
    m_audioInfoLabel->setStyleSheet("font-size: 10px; color: #858992;");
    m_audioInfoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_audioInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoRow->addWidget(m_titleLabel, 2);
    infoRow->addWidget(m_artistLabel, 1);
    infoRow->addWidget(m_albumLabel, 1);
    infoRow->addWidget(m_audioInfoLabel, 1);
    m_lblCurrentTime = new QLabel(QStringLiteral("0:00"), this);
    m_lblTotalTime = new QLabel(QStringLiteral("0:00"), this);
    for (auto* label : {m_lblCurrentTime, m_lblTotalTime}) {
        label->setStyleSheet("font-size: 10px; color: #A5A8B0;");
        label->setMinimumWidth(34);
    }
    infoRow->addWidget(m_lblCurrentTime);
    infoRow->addWidget(m_lblTotalTime);
    root->addWidget(infoBar, 1);

    connect(m_btnPlayPause, &QPushButton::clicked, this, &NowPlayingHeader::playPauseClicked);
    connect(m_btnPrev, &QPushButton::clicked, this, &NowPlayingHeader::prevClicked);
    connect(m_btnNext, &QPushButton::clicked, this, &NowPlayingHeader::nextClicked);
    connect(m_playbackModeButton, &QPushButton::clicked, this, [this]() {
        setPlaybackMode((m_playbackModeValue + 1) % 4);
        emit playbackModeChanged(m_playbackModeValue);
    });
    connect(m_waveform, &WaveformProgressWidget::seekRequested, this,
            [this](double progress) {
                if (m_totalDuration > 0.0) {
                    emit seekRequested(progress * m_totalDuration);
                }
            });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        emit volumeChanged(value / 100.0f);
    });
}

void NowPlayingHeader::setTrackInfo(const library::TrackInfo& info) {
    m_trackInfo = info;
    m_coverThumb->setPixmap(library::MetadataReader::loadCoverArt(info, 280));
    m_titleLabel->setText(info.title);
    m_artistLabel->setText(info.artist);
    m_albumLabel->setText(info.album);
    updateAudioInfoLabel();
}

void NowPlayingHeader::setAudioInfoVisibility(bool format, bool sampleRate,
                                              bool bitDepth, bool channels,
                                              bool bitrate)
{
    m_showAudioFormat = format;
    m_showSampleRate = sampleRate;
    m_showBitDepth = bitDepth;
    m_showChannels = channels;
    m_showBitrate = bitrate;
    updateAudioInfoLabel();
}

void NowPlayingHeader::updateAudioInfoLabel()
{
    QStringList details;
    if (m_showAudioFormat && !m_trackInfo.audioFormat.isEmpty()) {
        details.push_back(m_trackInfo.audioFormat);
    }
    if (m_showSampleRate && m_trackInfo.sampleRateHz > 0) {
        details.push_back(QStringLiteral("%1 kHz")
                              .arg(m_trackInfo.sampleRateHz / 1000.0, 0, 'f', 1));
    }
    if (m_showBitDepth && m_trackInfo.bitDepth > 0) {
        details.push_back(QStringLiteral("%1-bit").arg(m_trackInfo.bitDepth));
    }
    if (m_showChannels && m_trackInfo.channels > 0) {
        details.push_back(QStringLiteral("%1 ch").arg(m_trackInfo.channels));
    }
    if (m_showBitrate && m_trackInfo.bitrateKbps > 0) {
        details.push_back(QStringLiteral("%1 kb/s").arg(m_trackInfo.bitrateKbps));
    }

    const QString fullText = details.join(QStringLiteral(" · "));
    m_audioInfoLabel->setToolTip(fullText);
    m_audioInfoLabel->setText(fullText);
}

void NowPlayingHeader::setPlayingState(bool playing) {
    m_btnPlayPause->setIcon(QIcon(playing
        ? QStringLiteral(":/buttons/pause.png")
        : QStringLiteral(":/buttons/play.png")));
    m_btnPlayPause->setToolTip(
        playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void NowPlayingHeader::setPlaybackMode(int mode) {
    static const QStringList descriptions{
        QStringLiteral("顺序播放"), QStringLiteral("随机播放"),
        QStringLiteral("单曲循环"), QStringLiteral("列表循环")};
    static const QStringList icons{
        QStringLiteral(":/buttons/sequential.png"),
        QStringLiteral(":/buttons/shuffle.png"),
        QStringLiteral(":/buttons/repeat-one.png"),
        QStringLiteral(":/buttons/repeat-all.png")};
    if (mode < 0 || mode >= icons.size()) {
        return;
    }
    m_playbackModeValue = mode;
    m_playbackModeButton->setIcon(QIcon(icons[mode]));
    m_playbackModeButton->setToolTip(descriptions[mode]);
}

void NowPlayingHeader::setVolume(double volume) {
    const QSignalBlocker blocker(m_volumeSlider);
    const int value = static_cast<int>(std::clamp(volume, 0.0, 2.0) * 100.0);
    m_volumeSlider->setValue(value);
}

void NowPlayingHeader::updateProgress(double currentSec, double totalSec) {
    m_totalDuration = totalSec;
    m_lblCurrentTime->setText(formatTime(currentSec));
    m_lblTotalTime->setText(formatTime(totalSec));
    m_waveform->setProgress(totalSec > 0.0 ? currentSec / totalSec : 0.0);
}

void NowPlayingHeader::updateVisualization(
    const double* preLeft, const double* preRight,
    const double* postLeft, const double* postRight,
    std::size_t count, std::uint32_t sampleRate) {
    if (!preLeft || !preRight || count == 0) {
        return;
    }
    m_waveformSamples.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_waveformSamples[index] = (preLeft[index] + preRight[index]) * 0.5;
    }
    m_waveform->setSamples(m_waveformSamples.data(), m_waveformSamples.size());
    if (postLeft && postRight) {
        m_preDspWaterfall->pushSamples(preLeft, preRight, count, sampleRate);
        m_postDspWaterfall->pushSamples(postLeft, postRight, count, sampleRate);
    }
}

QString NowPlayingHeader::formatTime(double seconds) {
    int total = static_cast<int>(std::max(0.0, seconds));
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

} // namespace isac::ui
