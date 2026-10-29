#include "ISACMusicView.h"
#include "ui/widgets/HoverIconButton.h"
#include "ui/widgets/HoverSlider.h"
#include "ui/theme/ThemeManager.h"
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
#include <QToolButton>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QDataStream>
#include <QFontMetrics>
#include <QGridLayout>
#include <QResizeEvent>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr int kPlaybackControlsSize = 140;
constexpr int kPlaybackVolumeWidth = 24;
constexpr int kPlaybackCoverSize = 140;

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
        const int cellWidth = availableWidth / columns;
        const int coverSize = std::clamp(cellWidth - 24, 128, 196);
        setSpacing(0);
        setIconSize(QSize(coverSize, coverSize));
        setGridSize(QSize(cellWidth, coverSize + 62));
    }

    int m_lastWidth = -1;
};

class BalancedTrackHeader final : public QHeaderView {
public:
    explicit BalancedTrackHeader(QWidget* parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent)
    {}

    void applyAutomaticWidths()
    {
        const int availableWidth = viewport()->width();
        const int columnCount = count();
        if (availableWidth <= 0 || (columnCount != 4 && columnCount != 5) ||
            (availableWidth == m_lastAvailableWidth &&
             columnCount == m_lastColumnCount)) {
            return;
        }
        m_lastAvailableWidth = availableWidth;
        m_lastColumnCount = columnCount;
        const int minimumWidth = std::min(
            minimumSectionSize(), availableWidth / columnCount);
        const int durationWidth = std::clamp(
            kDurationWidth, minimumWidth,
            std::max(minimumWidth,
                     availableWidth - minimumWidth * (columnCount - 1)));
        const int trackNumberWidth = columnCount == 5
            ? std::clamp(kTrackNumberWidth, minimumWidth,
                         std::max(minimumWidth,
                                  availableWidth - durationWidth -
                                      minimumWidth * 3))
            : 0;
        int flexibleWidth = availableWidth - durationWidth - trackNumberWidth;
        std::array<int, 3> widths{};
        std::array<bool, 3> active{true, true, true};
        int remainingWeight = kColumnWeights[0] +
                              kColumnWeights[1] +
                              kColumnWeights[2];

        while (remainingWeight > 0) {
            bool clamped = false;
            for (std::size_t i = 0; i < widths.size(); ++i) {
                if (!active[i]) {
                    continue;
                }
                const int proposed =
                    flexibleWidth * kColumnWeights[i] / remainingWeight;
                if (proposed < minimumWidth) {
                    widths[i] = minimumWidth;
                    flexibleWidth -= minimumWidth;
                    remainingWeight -= kColumnWeights[i];
                    active[i] = false;
                    clamped = true;
                    break;
                }
            }
            if (clamped) {
                continue;
            }

            int remainingWidth = flexibleWidth;
            std::size_t lastActive = widths.size() - 1;
            while (!active[lastActive]) {
                --lastActive;
            }
            for (std::size_t i = 0; i < widths.size(); ++i) {
                if (!active[i]) {
                    continue;
                }
                widths[i] = i == lastActive
                    ? remainingWidth
                    : flexibleWidth * kColumnWeights[i] / remainingWeight;
                remainingWidth -= widths[i];
            }
            break;
        }

        if (columnCount == 5) {
            resizeSection(0, trackNumberWidth);
            resizeSection(1, widths[0]);
            resizeSection(2, widths[1]);
            resizeSection(3, widths[2]);
            resizeSection(4, durationWidth);
        } else {
            resizeSection(0, widths[0]);
            resizeSection(1, widths[1]);
            resizeSection(2, widths[2]);
            resizeSection(3, durationWidth);
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QHeaderView::resizeEvent(event);
        applyAutomaticWidths();
    }

private:
    static constexpr std::array<int, 3> kColumnWeights{4, 2, 3};
    static constexpr int kDurationWidth = 76;
    static constexpr int kTrackNumberWidth = 52;
    int m_lastAvailableWidth = -1;
    int m_lastColumnCount = -1;
};

class ResponsiveTrackTableView final : public QTableView {
public:
    using QTableView::QTableView;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QTableView::resizeEvent(event);
        static_cast<BalancedTrackHeader*>(horizontalHeader())
            ->applyAutomaticWidths();
    }
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

void configureTrackTableColumns(QTableView* table)
{
    auto* header = table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setStretchLastSection(false);
    for (int column = 0; column < 4; ++column) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    header->setMinimumSectionSize(1);
    static_cast<BalancedTrackHeader*>(header)->applyAutomaticWidths();
}

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
    return parent.isValid() ? 0 : (m_showTrackNumber ? 5 : 4);
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    if (!loadPage(index.row())) {
        return {};
    }
    const auto& track = m_page[static_cast<std::size_t>(index.row() - m_pageStart)];
    const int titleColumn = m_showTrackNumber ? 1 : 0;
    if (role == Qt::DecorationRole && index.column() == titleColumn) {
        if (const auto* cached = m_coverCache.object(index.row())) {
            return *cached;
        }

        const QPixmap cover = library::MetadataReader::loadCoverArt(track, 80);
        const QIcon icon = cover.isNull() ? m_defaultIcon : QIcon(cover);
        m_coverCache.insert(index.row(), new QIcon(icon));
        return icon;
    }
    const int artistColumn = titleColumn + 1;
    const int albumColumn = titleColumn + 2;
    const int durationColumn = titleColumn + 3;
    if (role == Qt::ForegroundRole &&
        (index.column() == artistColumn || index.column() == albumColumn)) {
        return ThemeManager::paletteColors().textSecondary;
    }
    if (role == Qt::ForegroundRole && index.column() == durationColumn) {
        return ThemeManager::paletteColors().textMuted;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case 0:
        return m_showTrackNumber && track.trackNumber > 0
            ? QString::number(track.trackNumber)
            : m_showTrackNumber ? QString{} : track.title;
    case 1:
        return m_showTrackNumber ? track.title : track.artist;
    case 2:
        return m_showTrackNumber ? track.artist : track.album;
    case 3:
        return m_showTrackNumber ? track.album : QString("%1:%2")
            .arg(track.durationSeconds / 60)
            .arg(track.durationSeconds % 60, 2, 10, QChar('0'));
    case 4: {
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
    case 0: return m_showTrackNumber
        ? QStringLiteral("#") : QStringLiteral("Title");
    case 1: return m_showTrackNumber
        ? QStringLiteral("Title") : QStringLiteral("Artist");
    case 2: return m_showTrackNumber
        ? QStringLiteral("Artist") : QStringLiteral("Album");
    case 3: return m_showTrackNumber
        ? QStringLiteral("Album") : QStringLiteral("Duration");
    case 4: return QStringLiteral("Duration");
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
    } else if (m_historyContext) {
        m_trackCount = std::max<qint64>(
            0, m_repository->playbackHistoryCount(m_searchText));
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

void TrackTableModel::setShowTrackNumber(bool visible) {
    if (m_showTrackNumber == visible) {
        return;
    }
    beginResetModel();
    m_showTrackNumber = visible;
    m_page.clear();
    m_pageStart = -1;
    m_coverCache.clear();
    endResetModel();
}

void TrackTableModel::setPlaylistId(std::optional<qint64> playlistId) {
    if (!m_playlistContext || m_playlistId != playlistId) {
        m_playlistContext = true;
        m_historyContext = false;
        m_playlistId = playlistId;
        m_artistFilter.clear();
        m_albumFilter.clear();
        refresh();
    }
}

void TrackTableModel::setCategoryFilter(const QString& artist,
                                        const QString& album) {
    if (m_artistFilter != artist || m_albumFilter != album ||
        m_playlistContext || m_historyContext) {
        m_playlistContext = false;
        m_historyContext = false;
        m_playlistId.reset();
        m_artistFilter = artist;
        m_albumFilter = album;
        refresh();
    }
}

void TrackTableModel::setHistoryMode(bool enabled) {
    if (m_historyContext != enabled || m_playlistContext) {
        m_historyContext = enabled;
        m_playlistContext = false;
        m_playlistId.reset();
        m_artistFilter.clear();
        m_albumFilter.clear();
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
    if (m_historyContext) {
        m_page = m_repository->queryPlaybackHistory(
            m_searchText, kPageSize, pageStart);
    } else if (m_playlistContext) {
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
    if (!m_repository || id <= 0 || m_historyContext ||
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
        if (m_historyContext) {
            const auto page = m_repository->queryPlaybackHistory(
                m_searchText, pageSize, static_cast<int>(offset));
            for (const auto& track : page) {
                ids.push_back(track.id);
            }
            if (page.empty()) {
                break;
            }
            continue;
        } else if (m_playlistContext) {
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
            return QStringLiteral("%1\n%2 tracks")
                .arg(group.artist)
                .arg(group.trackCount);
        }
        if (const auto* cached = m_coverCache.object(index.row())) {
            return *cached;
        }
        const auto tracks = m_repository->queryAlbumTracks(
            group.name, {}, library::TrackSort::TrackNumber, true, 1, 0);
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
        case 0: return QStringLiteral("Album");
        case 1: return QStringLiteral("Artist");
        case 2: return QStringLiteral("Tracks");
        default: return {};
        }
    }
    switch (section) {
    case 0: return QStringLiteral("Artist");
    case 1: return QStringLiteral("Tracks");
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

// ==================== ISACMusicLibraryPage ====================
ISACMusicLibraryPage::ISACMusicLibraryPage(library::LibraryRepository* repository,
                                   QWidget* parent)
    : QWidget(parent)
    , m_repository(repository) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 24, 0, 16);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与导入按钮栏
    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(32, 0, 32, 0);
    auto* pageTitle = new QLabel("Library", this);
    pageTitle->setStyleSheet("font-size: 26px; font-weight: bold; color: #FFFFFF;");

    m_scanStatus = new QLabel(this);
    m_scanStatus->setStyleSheet("color: #9499A6; font-size: 12px;");
    m_scanStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topBar->addWidget(pageTitle);
    topBar->addStretch();

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Search tracks, artists, or albums");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setMinimumWidth(220);
    m_searchInput->setMaximumWidth(360);
    m_searchInput->setFixedHeight(34);
    m_searchInput->setStyleSheet(
        "QLineEdit { background: #1E2028; color: #EAEAEA; border: 1px solid #343846; "
        "border-radius: 0; padding: 7px 9px; }");
    m_searchInput->hide();

    m_searchToggleButton = new QToolButton(this);
    m_searchToggleButton->setCheckable(true);
    m_searchToggleButton->setChecked(false);
    m_searchToggleButton->setIcon(QIcon::fromTheme(
        QStringLiteral("edit-find"),
        style()->standardIcon(QStyle::SP_FileDialogContentsView)));
    m_searchToggleButton->setFixedSize(34, 34);
    m_searchToggleButton->setToolTip(QStringLiteral("Show/hide search"));
    m_searchToggleButton->setStyleSheet(
        "QToolButton { color: #E6E8ED; background: transparent; border: none; }"
        "QToolButton:hover { background: #343740; }"
        "QToolButton:checked { background: #343740; }");
    m_resultCount = new QLabel(this);
    m_resultCount->setStyleSheet("color: #9499A6; font-size: 12px;");
    m_resultCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topBar->addWidget(m_searchInput);
    topBar->addWidget(m_searchToggleButton);

    // 2. 歌曲列表表格 (ISACMusic 极简无竖线风格)
    const QPixmap defaultRowCover = library::MetadataReader::generateDefaultCover("ISAC", 80);
    m_table = new ResponsiveTrackTableView(this);
    m_table->setHorizontalHeader(new BalancedTrackHeader(m_table));
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
    configureTrackTableColumns(m_table);
    m_table->horizontalHeader()->setSortIndicatorShown(false);

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
    m_libraryTabs->addTab(QStringLiteral("Tracks"));
    m_libraryTabs->addTab(QStringLiteral("Artists"));
    m_libraryTabs->addTab(QStringLiteral("Albums"));
    m_libraryTabs->addTab(QStringLiteral("History"));
    m_libraryTabs->setExpanding(false);
    m_libraryTabs->setStyleSheet(
        "QTabBar::tab { color: #A5AAB8; background: transparent; "
        "padding: 8px 18px; border-bottom: 2px solid transparent; }"
        "QTabBar::tab:hover { color: #F5F6F8; background: #2B2F39; "
        "border-bottom-color: #626875; }"
        "QTabBar::tab:selected { color: #FFFFFF; border-bottom-color: #FA2D48; }"
        "QTabBar::tab:selected:hover { background: #343A4B; "
        "border-bottom-color: #FF5268; }");
    m_groupBackButton = new QToolButton(this);
    m_groupBackButton->setIcon(QIcon::fromTheme(
        QStringLiteral("go-previous"),
        style()->standardIcon(QStyle::SP_ArrowBack)));
    m_groupBackButton->setFixedSize(34, 34);
    m_groupBackButton->setToolTip(QStringLiteral("Back"));
    m_groupBackButton->setStyleSheet(
        "QToolButton { color: #E6E8ED; background: transparent; border: none; }"
        "QToolButton:hover { background: #343740; }"
        "QToolButton:checked { background: #343740; }");
    m_groupBackButton->hide();

    m_albumViewMode = new QWidget(this);
    auto* albumViewModeLayout = new QHBoxLayout(m_albumViewMode);
    albumViewModeLayout->setContentsMargins(0, 0, 0, 0);
    albumViewModeLayout->setSpacing(0);
    m_albumListButton = new QToolButton(m_albumViewMode);
    m_albumCoverButton = new QToolButton(m_albumViewMode);
    m_albumListButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
    m_albumCoverButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_albumListButton->setToolTip(QStringLiteral("List view"));
    m_albumCoverButton->setToolTip(QStringLiteral("Cover view"));
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
    m_albumCoverView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_albumCoverView, &QListView::customContextMenuRequested,
            this, [this](const QPoint& position) {
        const QModelIndex index = m_albumCoverView->indexAt(position);
        if (!index.isValid()) {
            return;
        }
        const auto album = m_albumModel->groupAtRow(index.row());
        if (!album) {
            return;
        }
        m_albumCoverView->setCurrentIndex(index);

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
            QMenu::right-arrow {
                width: 10px;
                height: 10px;
                subcontrol-origin: padding;
                subcontrol-position: right center;
                right: 8px;
            }
        )");

        QAction* viewAlbum = menu.addAction(QStringLiteral("View Album"));
        connect(viewAlbum, &QAction::triggered, this, [this, album]() {
            m_libraryTabs->setCurrentIndex(2);
            showGroupTracks(*album);
        });

        QMenu* artistMenu = menu.addMenu(QStringLiteral("View Artist"));
        QStringList artists = album->artist.split(
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
            connect(action, &QAction::triggered, this, [this, artist]() {
                m_libraryTabs->setCurrentIndex(1);
                showGroupTracks({artist, {}, 0});
            });
        }

        menu.addSeparator();
        QMenu* addMenu = menu.addMenu(QStringLiteral("Add to Playlist"));
        const auto playlists = m_repository->playlists();
        if (playlists.empty()) {
            QAction* emptyAction = addMenu->addAction(
                QStringLiteral("No playlists available"));
            emptyAction->setEnabled(false);
        } else {
            for (const auto& playlist : playlists) {
                QAction* action = addMenu->addAction(playlist.name);
                connect(action, &QAction::triggered, this,
                        [this, albumName = album->name,
                         playlistId = playlist.id]() {
                    constexpr int pageSize = 10000;
                    std::vector<qint64> trackIds;
                    for (int offset = 0;; offset += pageSize) {
                        const auto page = m_repository->queryAlbumTrackIds(
                            albumName, {}, library::TrackSort::TrackNumber, true,
                            pageSize, offset);
                        if (page.empty()) {
                            break;
                        }
                        trackIds.insert(trackIds.end(),
                                        page.begin(), page.end());
                        if (static_cast<int>(page.size()) < pageSize) {
                            break;
                        }
                    }
                    const auto added = m_repository->addTracksToPlaylist(
                        playlistId, trackIds);
                    if (!added) {
                        m_scanStatus->setText(
                            QStringLiteral("Failed to add to playlist: %1")
                                .arg(m_repository->lastError()));
                        return;
                    }
                    m_scanStatus->setText(
                        QStringLiteral("Added %1 tracks, skipped %2 duplicates")
                            .arg(*added)
                            .arg(static_cast<qint64>(trackIds.size()) -
                                 *added));
                });
            }
        }
        menu.exec(m_albumCoverView->viewport()->mapToGlobal(position));
    });
    m_libraryContent = new QStackedWidget(this);
    m_libraryContent->addWidget(m_table);
    m_libraryContent->addWidget(m_albumCoverView);

    mainLayout->addLayout(topBar);
    auto* viewBar = new QHBoxLayout();
    viewBar->setContentsMargins(32, 0, 32, 0);
    viewBar->addWidget(m_libraryTabs);
    viewBar->addWidget(m_groupBackButton);
    viewBar->addWidget(m_albumViewMode);
    viewBar->addStretch();
    viewBar->addWidget(m_scanStatus);
    viewBar->addSpacing(16);
    viewBar->addWidget(m_resultCount);

    auto* viewBarContainer = new QWidget(this);
    auto* viewBarContainerLayout = new QVBoxLayout(viewBarContainer);
    viewBarContainerLayout->setContentsMargins(0, 0, 0, 0);
    viewBarContainerLayout->setSpacing(0);
    viewBarContainerLayout->addLayout(viewBar);
    auto* viewSeparator = new QFrame(viewBarContainer);
    viewSeparator->setFrameShape(QFrame::NoFrame);
    viewSeparator->setFixedHeight(1);
    viewSeparator->setStyleSheet(QStringLiteral(
        "background-color: #30333D;"));
    viewBarContainerLayout->addWidget(viewSeparator);
    mainLayout->addWidget(viewBarContainer);

    auto* libraryContentContainer = new QWidget(this);
    auto* libraryContentLayout = new QVBoxLayout(libraryContentContainer);
    libraryContentLayout->setContentsMargins(32, 0, 32, 0);
    libraryContentLayout->setSpacing(0);
    libraryContentLayout->addWidget(m_libraryContent, 1);
    mainLayout->addWidget(libraryContentContainer, 1);

    connect(m_searchToggleButton, &QToolButton::toggled, this,
            [this](bool visible) {
        m_searchInput->setVisible(visible);
        if (visible) {
            m_searchInput->setFocus();
            m_searchInput->selectAll();
        }
    });
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
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked,
            this, [this](int section) {
        if ((m_libraryView != 0 && !m_showingGroupTracks) ||
            section < 0 ||
            section > (m_showingGroupTracks && m_libraryView == 2 ? 4 : 3)) {
            return;
        }
        const bool albumTracks = m_showingGroupTracks && m_libraryView == 2;
        const library::TrackSort sort = [&]() {
            if (albumTracks) {
                switch (section) {
                case 0: return library::TrackSort::TrackNumber;
                case 1: return library::TrackSort::Title;
                case 2: return library::TrackSort::Artist;
                case 3: return library::TrackSort::Album;
                default: return library::TrackSort::Duration;
                }
            }
            switch (section) {
            case 0: return library::TrackSort::Title;
            case 1: return library::TrackSort::Artist;
            case 2: return library::TrackSort::Album;
            default: return library::TrackSort::Duration;
            }
        }();
        const library::TrackSort defaultSort = albumTracks
            ? library::TrackSort::TrackNumber
            : library::TrackSort::AddedAt;
        if (sort == library::TrackSort::Title &&
            m_trackSort == library::TrackSort::Title &&
            !m_trackSortAscending) {
            m_trackSort = defaultSort;
            m_trackSortAscending = true;
            if (defaultSort == library::TrackSort::AddedAt) {
                m_table->horizontalHeader()->setSortIndicatorShown(false);
            } else {
                m_table->horizontalHeader()->setSortIndicator(
                    0, Qt::AscendingOrder);
                m_table->horizontalHeader()->setSortIndicatorShown(true);
            }
        } else if (m_trackSort == sort) {
            m_trackSortAscending = !m_trackSortAscending;
            m_table->horizontalHeader()->setSortIndicator(
                section, m_trackSortAscending
                    ? Qt::AscendingOrder : Qt::DescendingOrder);
            m_table->horizontalHeader()->setSortIndicatorShown(true);
        } else {
            m_trackSort = sort;
            m_trackSortAscending = true;
            m_table->horizontalHeader()->setSortIndicator(
                section, Qt::AscendingOrder);
            m_table->horizontalHeader()->setSortIndicatorShown(true);
        }
        m_tableModel->setSort(m_trackSort, m_trackSortAscending);
        updateResultCount();
    });
    connect(m_libraryTabs, &QTabBar::currentChanged,
            this, [this](int index) { showLibraryView(index); });
    connect(m_groupBackButton, &QToolButton::clicked, this, [this]() {
        m_showingGroupTracks = false;
        m_tableModel->setCategoryFilter({}, {});
        showLibraryView(m_libraryView);
    });
    updateResultCount();

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        if (m_libraryView == 0 || m_libraryView == 3 ||
            m_showingGroupTracks) {
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
        if (m_libraryView != 0 && m_libraryView != 3 &&
            !m_showingGroupTracks) {
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
        const auto selectedTrack = selectedTrackIds.size() == 1
            ? m_tableModel->trackAtRow(selectedRows.front().row())
            : std::optional<library::TrackInfo>{};

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
            QStringLiteral("Add to Playlist"));
        const auto playlists = m_repository->playlists();
        if (playlists.empty()) {
            QAction* emptyAction = addMenu->addAction(
                QStringLiteral("No playlists available"));
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
                            QStringLiteral("Failed to add to playlist: %1")
                                .arg(m_repository->lastError()));
                        return;
                    }
                    m_scanStatus->setText(
                        QStringLiteral("Added %1 tracks, skipped %2 duplicates")
                            .arg(*added)
                            .arg(static_cast<qint64>(selectedTrackIds.size()) -
                                 *added));
                });
            }
        }
        if (selectedTrack) {
            menu.addSeparator();
            QAction* playNextAction =
                menu.addAction(QStringLiteral("Play Next"));
            connect(playNextAction, &QAction::triggered, this,
                    [this, trackId = selectedTrack->id]() {
                emit trackPlayNextRequested(trackId);
            });
            QMenu* artistMenu = menu.addMenu(QStringLiteral("View Artist"));
            QStringList artists = selectedTrack->artist.split(
                QRegularExpression(QStringLiteral("\\s*/\\s*")),
                Qt::SkipEmptyParts);
            for (QString& artist : artists) {
                artist = artist.trimmed();
            }
            artists.removeAll(QString{});
            if (artists.isEmpty()) {
                artists.push_back(QStringLiteral("Unknown Artist"));
            }
            for (const QString& name : artists) {
                QAction* action = artistMenu->addAction(name);
                connect(action, &QAction::triggered, this, [this, name]() {
                    m_libraryTabs->setCurrentIndex(1);
                    showGroupTracks({name, {}, 0});
                });
            }
            const QString albumName = selectedTrack->album.trimmed().isEmpty()
                ? QStringLiteral("Unknown Album")
                : selectedTrack->album;
            QAction* albumAction = menu.addAction(
                QStringLiteral("View Album: %1").arg(
                    albumName));
            connect(albumAction, &QAction::triggered, this,
                    [this, albumName]() {
                m_libraryTabs->setCurrentIndex(2);
                showGroupTracks({albumName, {}, 0});
            });
        }
        menu.exec(m_table->viewport()->mapToGlobal(position));
    });
}

void ISACMusicLibraryPage::toggleSearchInput()
{
    m_searchToggleButton->setChecked(!m_searchToggleButton->isChecked());
}

void ISACMusicLibraryPage::refreshTracks() {
    m_tableModel->refresh();
    m_artistModel->refresh();
    m_albumModel->refresh();
    updateResultCount();
}

void ISACMusicLibraryPage::refreshPlaybackHistory() {
    if (m_libraryView == 3) {
        m_tableModel->refresh();
        updateResultCount();
    }
}

void ISACMusicLibraryPage::setScanStatus(const QString& status) {
    m_scanStatus->setText(status);
}

void ISACMusicLibraryPage::showArtist(const QString& artist) {
    m_libraryTabs->setCurrentIndex(1);
    showGroupTracks({artist, {}, 0});
}

void ISACMusicLibraryPage::showAlbum(const QString& album) {
    m_libraryTabs->setCurrentIndex(2);
    showGroupTracks({album, {}, 0});
}

std::optional<library::TrackInfo> ISACMusicLibraryPage::trackAtRow(int row) const {
    return m_tableModel->trackAtRow(row);
}

int ISACMusicLibraryPage::trackCount() const {
    return m_tableModel->rowCount();
}

std::vector<qint64> ISACMusicLibraryPage::trackIds() const {
    return m_tableModel->trackIds();
}

int ISACMusicLibraryPage::rowForTrackId(qint64 id) const {
    return m_tableModel->rowForTrackId(id);
}

void ISACMusicLibraryPage::showLibraryView(int view) {
    m_libraryView = std::clamp(view, 0, 3);
    m_showingGroupTracks = false;
    m_tableModel->setShowTrackNumber(false);
    if (m_trackSort == library::TrackSort::TrackNumber) {
        m_trackSort = library::TrackSort::AddedAt;
        m_trackSortAscending = true;
    }
    m_tableModel->setSort(m_trackSort, m_trackSortAscending);
    m_groupBackButton->hide();
    m_tableModel->setHistoryMode(m_libraryView == 3);
    if (m_libraryView == 0) {
        m_tableModel->setCategoryFilter({}, {});
        m_table->setModel(m_tableModel);
        static_cast<BalancedTrackHeader*>(m_table->horizontalHeader())
            ->applyAutomaticWidths();
        m_libraryContent->setCurrentWidget(m_table);
    } else if (m_libraryView == 3) {
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
    m_table->horizontalHeader()->setSectionsClickable(m_libraryView == 0);
    m_table->horizontalHeader()->setSortIndicatorShown(
        m_libraryView == 0 && m_trackSort != library::TrackSort::AddedAt);
    updateResultCount();
}

void ISACMusicLibraryPage::openGroupAtRow(int row) {
    const auto* model = m_libraryView == 1 ? m_artistModel : m_albumModel;
    const auto group = model->groupAtRow(row);
    if (group) {
        showGroupTracks(*group);
    }
}

void ISACMusicLibraryPage::showGroupTracks(const library::LibraryGroup& group) {
    m_showingGroupTracks = true;
    const bool albumTracks = m_libraryView == 2;
    m_tableModel->setShowTrackNumber(albumTracks);
    if (albumTracks) {
        m_trackSort = library::TrackSort::TrackNumber;
        m_trackSortAscending = true;
    } else if (m_trackSort == library::TrackSort::TrackNumber) {
        m_trackSort = library::TrackSort::AddedAt;
        m_trackSortAscending = true;
    }
    const QString artist = m_libraryView == 1 ? group.name : QString{};
    const QString album = m_libraryView == 2 ? group.name : QString{};
    m_tableModel->setCategoryFilter(artist, album);
    m_tableModel->setSort(m_trackSort, m_trackSortAscending);
    m_table->setModel(m_tableModel);
    static_cast<BalancedTrackHeader*>(m_table->horizontalHeader())
        ->applyAutomaticWidths();
    m_libraryContent->setCurrentWidget(m_table);
    m_albumViewMode->hide();
    m_groupBackButton->setToolTip(
        QStringLiteral("Back to %1").arg(m_libraryView == 1
            ? QStringLiteral("Artists") : QStringLiteral("Albums")));
    m_groupBackButton->show();
    m_table->horizontalHeader()->setSectionsClickable(true);
    if (albumTracks) {
        m_table->horizontalHeader()->setSortIndicator(
            0, Qt::AscendingOrder);
        m_table->horizontalHeader()->setSortIndicatorShown(true);
    } else {
        m_table->horizontalHeader()->setSortIndicatorShown(
            m_trackSort != library::TrackSort::AddedAt);
    }
    updateResultCount();
}

void ISACMusicLibraryPage::updateResultCount() {
    if (m_showingGroupTracks || m_libraryView == 0) {
        m_resultCount->setText(
            QStringLiteral("%1 tracks").arg(m_tableModel->trackCount()));
    } else if (m_libraryView == 3) {
        m_resultCount->setText(
            QStringLiteral("%1 plays").arg(m_tableModel->trackCount()));
    } else if (m_libraryView == 1) {
        m_resultCount->setText(
            QStringLiteral("%1 artists").arg(m_artistModel->groupCount()));
    } else {
        m_resultCount->setText(
            QStringLiteral("%1 albums").arg(m_albumModel->groupCount()));
    }
}

// ==================== PlaylistPage ====================
PlaylistPage::PlaylistPage(library::LibraryRepository* repository,
                           QWidget* parent)
    : QWidget(parent)
    , m_repository(repository)
{
    auto* playlistPanel = new QWidget(this);
    playlistPanel->setMinimumWidth(160);
    playlistPanel->setMaximumWidth(420);
    playlistPanel->setStyleSheet(
        "QWidget { background: #191B22; border-right: 1px solid #2B2F3C; }"
        "QListWidget { background: #191B22; color: #C9CDD8; "
        "border: none; padding: 0; outline: none; }"
        "QListWidget::item { padding: 12px 14px; border: none; }"
        "QListWidget::item:hover { background: #252934; color: #F2F3F5; }"
        "QListWidget::item:selected { background: #343A4B; color: white; "
        "font-weight: bold; }");
    auto* playlistLayout = new QVBoxLayout(playlistPanel);
    playlistLayout->setContentsMargins(0, 0, 0, 0);
    playlistLayout->setSpacing(0);

    m_playlistList = new QListWidget(playlistPanel);
    m_playlistList->setFrameShape(QFrame::NoFrame);
    m_playlistList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    playlistLayout->addWidget(m_playlistList, 1);

    auto* tracksPanel = new QWidget(this);
    m_tracksPanel = tracksPanel;
    auto* tracksLayout = new QVBoxLayout(tracksPanel);
    tracksLayout->setContentsMargins(24, 20, 24, 16);
    tracksLayout->setSpacing(10);
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    m_playlistTitleLabel = new QLabel(QStringLiteral("Playlists"), tracksPanel);
    m_playlistTitleLabel->setStyleSheet(
        "font-size: 24px; font-weight: bold; color: #FFFFFF;");
    m_searchInput = new QLineEdit(tracksPanel);
    m_searchInput->setPlaceholderText(QStringLiteral("Filter current playlist"));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setMinimumWidth(220);
    m_searchInput->setFixedHeight(34);
    m_searchInput->setStyleSheet(
        "QLineEdit { background: #1E2028; color: #EAEAEA; "
            "border: 1px solid #343846; border-radius: 0; padding: 7px 9px; }");
    m_addTracksButton = new QPushButton(QStringLiteral("Add Tracks"), tracksPanel);
    m_removeTracksButton = new QPushButton(QStringLiteral("Remove from Playlist"), tracksPanel);
    for (auto* button : {m_addTracksButton, m_removeTracksButton}) {
        button->setStyleSheet(
            "QPushButton { background: #252834; color: #E0E4F0; "
            "border: 1px solid #343846; border-radius: 0; padding: 7px 10px; }"
            "QPushButton:hover { background: #343A4B; }"
            "QPushButton:disabled { color: #666B78; }");
    }
    m_toolsButton = new QToolButton(tracksPanel);
    m_toolsButton->setCheckable(true);
    m_toolsButton->setIcon(QIcon::fromTheme(
        QStringLiteral("view-filter"),
        style()->standardIcon(QStyle::SP_FileDialogContentsView)));
    m_toolsButton->setFixedSize(34, 34);
    m_toolsButton->setToolTip(QStringLiteral("Show playlist tools (Ctrl+F)"));
    m_toolsButton->setStyleSheet(
        "QToolButton { background: #252834; color: #E0E4F0; "
        "border: 1px solid #343846; border-radius: 0; }"
        "QToolButton:hover, QToolButton:checked { background: #343A4B; "
        "color: #FFFFFF; }");

    m_toolsPanel = new QFrame(tracksPanel);
    m_toolsPanel->setObjectName(QStringLiteral("playlistToolsPanel"));
    m_toolsPanel->setFrameShape(QFrame::StyledPanel);
    m_toolsPanel->setMinimumWidth(320);
    m_toolsPanel->setMaximumWidth(480);
    m_toolsPanel->setStyleSheet(
        "QFrame#playlistToolsPanel { background: #20232B; "
        "border: 1px solid #4A4F5D; }");
    auto* toolsLayout = new QHBoxLayout(m_toolsPanel);
    toolsLayout->setContentsMargins(8, 8, 8, 8);
    toolsLayout->setSpacing(8);
    toolsLayout->addWidget(m_searchInput, 1);
    toolsLayout->addWidget(m_addTracksButton);
    toolsLayout->addWidget(m_removeTracksButton);
    auto* toolsCloseButton = new QToolButton(m_toolsPanel);
    toolsCloseButton->setIcon(QIcon::fromTheme(
        QStringLiteral("window-close"),
        style()->standardIcon(QStyle::SP_DialogCloseButton)));
    toolsCloseButton->setFixedSize(30, 30);
    toolsCloseButton->setToolTip(QStringLiteral("Hide playlist tools"));
    toolsCloseButton->setStyleSheet(
        "QToolButton { background: transparent; color: #A5AAB8; "
        "border: none; }"
        "QToolButton:hover { background: #343A4B; color: #FFFFFF; }");
    toolsLayout->addWidget(toolsCloseButton);
    m_toolsPanel->adjustSize();
    m_toolsPanel->hide();

    toolbar->addWidget(m_playlistTitleLabel);
    toolbar->addStretch();
    toolbar->addWidget(m_toolsButton);
    tracksLayout->addLayout(toolbar);

    const QPixmap defaultCover =
        library::MetadataReader::generateDefaultCover("ISAC", 80);
    m_table = new ResponsiveTrackTableView(tracksPanel);
    m_table->setHorizontalHeader(new BalancedTrackHeader(m_table));
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
    configureTrackTableColumns(m_table);
    m_table->horizontalHeader()->setSectionsClickable(false);
    m_table->setStyleSheet(
        "QTableView { background: transparent; border: none; color: #EAEAEA; "
        "font-size: 13px; alternate-background-color: #1A1C22; }"
        "QHeaderView::section { background: transparent; color: #8A8F9E; "
        "font-weight: bold; border: none; border-bottom: 1px solid #2A2E39; "
        "padding: 6px; }"
        "QTableView::item { padding: 6px; border-bottom: 1px solid #1E2129; }"
        "QTableView::item:selected { background: #FA2D48; color: white; }");
    tracksLayout->addWidget(m_table, 1);

    m_statusLabel = new QLabel(tracksPanel);
    m_statusLabel->setStyleSheet("color: #9499A6; font-size: 12px;");
    tracksLayout->addWidget(m_statusLabel);

    m_playlistSplitter = new QSplitter(Qt::Horizontal, this);
    m_playlistSplitter->setChildrenCollapsible(false);
    m_playlistSplitter->setHandleWidth(5);
    m_playlistSplitter->setStyleSheet(
        "QSplitter::handle { background: #2B2F3C; }"
        "QSplitter::handle:hover { background: #FA2D48; }");
    m_playlistSplitter->addWidget(playlistPanel);
    m_playlistSplitter->addWidget(tracksPanel);
    m_playlistSplitter->setStretchFactor(0, 0);
    m_playlistSplitter->setStretchFactor(1, 1);
    m_playlistSplitter->setSizes({
        kPlaybackVolumeWidth + kPlaybackCoverSize, 800});
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_playlistSplitter);

    m_playlistList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlistList, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& position) {
        const QModelIndex index = m_playlistList->indexAt(position);
        if (index.isValid()) {
            m_playlistList->setCurrentIndex(index);
        }

        QMenu menu(this);
        QAction* createAction =
            menu.addAction(QStringLiteral("New Playlist"));
        QAction* renameAction = nullptr;
        QAction* deleteAction = nullptr;
        if (index.isValid()) {
            menu.addSeparator();
            renameAction = menu.addAction(QStringLiteral("Rename Playlist"));
            deleteAction = menu.addAction(QStringLiteral("Delete Playlist"));
        }

        QAction* selected = menu.exec(
            m_playlistList->viewport()->mapToGlobal(position));
        if (selected == createAction) {
            createPlaylist();
        } else if (selected == renameAction) {
            renameSelectedPlaylist();
        } else if (selected == deleteAction) {
            deleteSelectedPlaylist();
        }
    });
    connect(m_playlistList, &QListWidget::currentRowChanged,
            this, [this](int) {
                const auto id = selectedPlaylistId();
                m_tableModel->setPlaylistId(id);
                m_searchInput->clear();
                updatePlaylistTitle();
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
        m_statusLabel->setText(QStringLiteral("Removed %1 tracks").arg(removed));
    });
    connect(m_toolsButton, &QToolButton::toggled, this,
            [this](bool visible) { setToolsPanelVisible(visible); });
    connect(toolsCloseButton, &QToolButton::clicked, this,
            [this]() { setToolsPanelVisible(false); });
    connect(m_table, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) {
                const auto track = m_tableModel->trackAtRow(index.row());
                if (track) {
                    emit trackDoubleClicked(track->id);
                }
            });

    refresh();
}

bool PlaylistPage::toolsPanelVisible() const
{
    return m_toolsPanel && m_toolsPanel->isVisible();
}

void PlaylistPage::toggleToolsPanel()
{
    setToolsPanelVisible(!toolsPanelVisible());
}

void PlaylistPage::setToolsPanelVisible(bool visible)
{
    if (!m_toolsPanel || !m_toolsButton) {
        return;
    }
    if (m_toolsButton->isChecked() != visible) {
        const QSignalBlocker blocker(m_toolsButton);
        m_toolsButton->setChecked(visible);
    }
    m_toolsPanel->setVisible(visible);
    if (visible) {
        resizeEvent(nullptr);
        m_toolsPanel->raise();
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }
    emit toolsVisibilityChanged(visible);
}

void PlaylistPage::resizeEvent(QResizeEvent* event)
{
    if (event) {
        QWidget::resizeEvent(event);
    }
    if (!m_tracksPanel || !m_toolsPanel) {
        return;
    }
    const int availableWidth = std::max(260, m_tracksPanel->width() - 48);
    const int panelWidth = std::clamp(availableWidth, 260, 480);
    m_toolsPanel->setFixedWidth(panelWidth);
    const int x = std::max(0, m_tracksPanel->width() - panelWidth - 24);
    m_toolsPanel->move(x, 12);
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
    updatePlaylistTitle();
    updateControls();
}

void PlaylistPage::updateControls()
{
    const bool hasPlaylist = selectedPlaylistId().has_value();
    const bool hasSelection = m_table->selectionModel() &&
        !m_table->selectionModel()->selectedRows().isEmpty();
    m_addTracksButton->setEnabled(hasPlaylist);
    m_removeTracksButton->setEnabled(hasPlaylist && hasSelection);
}

void PlaylistPage::updatePlaylistTitle()
{
    const auto* item = m_playlistList->currentItem();
    const QString title = item
        ? item->data(Qt::UserRole + 1).toString()
        : QStringLiteral("Playlists");
    m_playlistTitleLabel->setText(
        title.isEmpty() ? QStringLiteral("Playlists") : title);
}

void PlaylistPage::createPlaylist()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Playlist"), QStringLiteral("Playlist name"),
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
}

void PlaylistPage::renameSelectedPlaylist()
{
    const auto id = selectedPlaylistId();
    if (!id || !m_playlistList->currentItem()) {
        return;
    }
    const QString current =
        m_playlistList->currentItem()->data(Qt::UserRole + 1).toString();
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename Playlist"), QStringLiteral("Playlist name"),
        QLineEdit::Normal, current, &accepted);
    if (!accepted) {
        return;
    }
    if (!m_repository->renamePlaylist(*id, name)) {
        m_statusLabel->setText(m_repository->lastError());
        return;
    }
    refreshPlaylistList(*id);
}

void PlaylistPage::deleteSelectedPlaylist()
{
    const auto id = selectedPlaylistId();
    if (!id || QMessageBox::question(
            this, QStringLiteral("Delete Playlist"),
            QStringLiteral("Delete this playlist? Files in the library will not be deleted."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!m_repository->deletePlaylist(*id)) {
        m_statusLabel->setText(m_repository->lastError());
        return;
    }
    refreshPlaylistList();
}

void PlaylistPage::openAddTracksDialog()
{
    const auto playlistId = selectedPlaylistId();
    if (!playlistId) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Tracks to Playlist"));
    dialog.resize(760, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Search tracks, artists, or albums"));
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
    m_statusLabel->setText(QStringLiteral("Added %1 tracks").arg(added));
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
    m_contentLayout = new QHBoxLayout(contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_controlGroup = new QWidget(this);
    m_controlGroup->setFixedSize(
        kPlaybackControlsSize, kPlaybackControlsSize);
    m_controlGroup->setStyleSheet(QStringLiteral("background: #292C34;"));
    auto* controlGrid = new QGridLayout(m_controlGroup);
    controlGrid->setContentsMargins(0, 0, 0, 0);
    controlGrid->setHorizontalSpacing(0);
    controlGrid->setVerticalSpacing(0);
    m_playbackModeButton = new HoverIconButton(this);
    m_btnPrev = new HoverIconButton(this);
    m_btnPlayPause = new HoverIconButton(this);
    m_btnNext = new HoverIconButton(this);
    for (auto* button : {m_btnPrev, m_btnPlayPause, m_btnNext,
                         m_playbackModeButton}) {
        button->setIconSize(QSize(31, 31));
        static_cast<HoverIconButton*>(button)->setNormalIconSize(
            QSize(31, 31));
        static_cast<HoverIconButton*>(button)->setHoverScale(1.2);
    }
    m_btnPrev->setToolTip(QStringLiteral("Previous track"));
    m_btnNext->setToolTip(QStringLiteral("Next track"));
    m_btnPlayPause->setToolTip(QStringLiteral("Play"));
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
    m_volumeSlider = new HoverSlider(Qt::Vertical, this);
    m_volumeSlider->setRange(0, qRound(m_volumeMaximum * 100.0));
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedSize(
        kPlaybackVolumeWidth, kPlaybackControlsSize);
    m_volumeSlider->setToolTip(QStringLiteral("Volume"));

    m_coverThumb = new QLabel(this);
    m_coverThumb->setFixedSize(kPlaybackCoverSize, kPlaybackCoverSize);
    m_coverThumb->setScaledContents(true);
    m_coverThumb->setPixmap(library::MetadataReader::generateDefaultCover("ISAC", 280));
    m_coverThumb->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_coverThumb, &QLabel::customContextMenuRequested, this,
            [this](const QPoint& position) {
        emit coverContextMenuRequested(
            m_trackInfo.id, m_coverThumb->mapToGlobal(position));
    });
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
    m_contentLayout->addLayout(visualColumn, 1);
    setControlOrder(0);

    m_waveformHoverLabel = new QLabel(this);
    m_waveformHoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_waveformHoverLabel->setAlignment(Qt::AlignCenter);
    m_waveformHoverLabel->setStyleSheet(
        "QLabel { color: #F5F6F8; background: #20232B; "
        "border: 1px solid #626875; padding: 2px 6px; }");
    m_waveformHoverLabel->hide();
    m_waveformHoverLabel->raise();
    connect(m_waveform, &WaveformProgressWidget::hoverPreviewChanged,
            this, &NowPlayingHeader::updateWaveformHoverLabel);

    root->addWidget(contentWidget);

    auto* infoBar = new QWidget(this);
    infoBar->setObjectName(QStringLiteral("trackInfoBar"));
    infoBar->setStyleSheet(
        "QWidget#trackInfoBar { background: transparent; "
        "border-bottom: 1px solid #30333D; }");
    auto* infoRow = new QHBoxLayout(infoBar);
    infoRow->setContentsMargins(8, 0, 0, 2);
    infoRow->setSpacing(8);
    m_titleLabel = new QLabel(QStringLiteral("Not Playing"), this);
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
    m_volumeLabel = new QLabel(QStringLiteral("Gain 1.00x"), this);
    m_volumeLabel->setStyleSheet("font-size: 10px; color: #A5A8B0;");
    m_volumeLabel->setMinimumWidth(68);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    infoRow->addWidget(m_titleLabel, 2);
    infoRow->addWidget(m_artistLabel, 1);
    infoRow->addWidget(m_albumLabel, 1);
    infoRow->addWidget(m_audioInfoLabel, 1);
    infoRow->addWidget(m_volumeLabel);
    auto* queueButton = new QToolButton(infoBar);
    queueButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    queueButton->setToolTip(QStringLiteral("Current playback queue"));
    queueButton->setFixedSize(28, 28);
    queueButton->setStyleSheet(
        "QToolButton { color: #E6E8ED; background: transparent; border: none; }"
        "QToolButton:hover { background: #343740; }");
    connect(queueButton, &QToolButton::clicked, this,
            &NowPlayingHeader::queueViewRequested);
    infoRow->addWidget(queueButton);
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
        m_volumeFactor = value / 100.0;
        m_volumeLabel->setText(
            QStringLiteral("Gain %1x").arg(m_volumeFactor, 0, 'f', 2));
        emit volumeChanged(static_cast<float>(m_volumeFactor));
    });
    updateControlIcons();
    if (auto* themeManager = ThemeManager::instance()) {
        connect(themeManager, &ThemeManager::themeChanged, this,
                &NowPlayingHeader::updateControlIcons);
    }
}

void NowPlayingHeader::updateControlIcons()
{
    const QColor iconColor = ThemeManager::paletteColors().textPrimary;
    const auto themedIcon = [&iconColor](const QString& resourcePath) {
        QPixmap pixmap(resourcePath);
        if (pixmap.isNull()) {
            return QIcon(resourcePath);
        }
        pixmap = pixmap.scaled(64, 64, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), iconColor);
        return QIcon(pixmap);
    };

    m_btnPrev->setIcon(themedIcon(QStringLiteral(":/buttons/previous.png")));
    m_btnNext->setIcon(themedIcon(QStringLiteral(":/buttons/next.png")));
    m_btnPlayPause->setIcon(themedIcon(
        m_isPlaying
            ? QStringLiteral(":/buttons/pause.png")
            : QStringLiteral(":/buttons/play.png")));

    static const QStringList modeIcons{
        QStringLiteral(":/buttons/sequential.png"),
        QStringLiteral(":/buttons/shuffle.png"),
        QStringLiteral(":/buttons/repeat-one.png"),
        QStringLiteral(":/buttons/repeat-all.png")};
    if (m_playbackModeValue >= 0 && m_playbackModeValue < modeIcons.size()) {
        m_playbackModeButton->setIcon(
            themedIcon(modeIcons[m_playbackModeValue]));
    }
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

void NowPlayingHeader::updateWaveformHoverLabel(bool visible, double progress)
{
    m_hoverPreviewVisible = visible;
    m_hoverPreviewProgress = std::clamp(progress, 0.0, 1.0);
    if (!visible || m_totalDuration <= 0.0 || !m_waveform) {
        m_waveformHoverLabel->hide();
        return;
    }

    m_waveformHoverLabel->setText(
        formatTime(m_hoverPreviewProgress * m_totalDuration));
    m_waveformHoverLabel->adjustSize();

    const QPoint waveformTopLeft = m_waveform->mapTo(this, QPoint(0, 0));
    const int waveformWidth = m_waveform->width();
    const int centerX = waveformTopLeft.x() +
        qRound(m_hoverPreviewProgress * waveformWidth);
    const int labelX = std::clamp(
        centerX - m_waveformHoverLabel->width() / 2,
        waveformTopLeft.x(),
        waveformTopLeft.x() + waveformWidth -
            m_waveformHoverLabel->width());
    const int labelY = waveformTopLeft.y() + m_waveform->height() - 1;
    m_waveformHoverLabel->move(labelX, labelY);
    m_waveformHoverLabel->raise();
    m_waveformHoverLabel->show();
}

void NowPlayingHeader::setPlayingState(bool playing) {
    m_isPlaying = playing;
    m_btnPlayPause->setToolTip(
        playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    updateControlIcons();
}

void NowPlayingHeader::setPlaybackMode(int mode) {
    static const QStringList descriptions{
        QStringLiteral("Sequential"), QStringLiteral("Shuffle"),
        QStringLiteral("Repeat One"), QStringLiteral("Repeat All")};
    if (mode < 0 || mode >= descriptions.size()) {
        return;
    }
    m_playbackModeValue = mode;
    updateControlIcons();
    m_playbackModeButton->setToolTip(descriptions[mode]);
}

void NowPlayingHeader::setVolumeMaximum(double maximum)
{
    const double previousVolume = m_volumeFactor;
    m_volumeMaximum = std::clamp(maximum, 1.0, 4.0);
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setRange(0, qRound(m_volumeMaximum * 100.0));
    m_volumeFactor = std::clamp(m_volumeFactor, 0.0, m_volumeMaximum);
    m_volumeSlider->setValue(qRound(m_volumeFactor * 100.0));
    m_volumeLabel->setText(
        QStringLiteral("Gain %1x").arg(m_volumeFactor, 0, 'f', 2));
    if (!qFuzzyCompare(previousVolume, m_volumeFactor)) {
        emit volumeChanged(static_cast<float>(m_volumeFactor));
    }
}

void NowPlayingHeader::setVolume(double volume) {
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeFactor = std::clamp(volume, 0.0, m_volumeMaximum);
    m_volumeLabel->setText(
        QStringLiteral("Gain %1x").arg(m_volumeFactor, 0, 'f', 2));
    const int value = static_cast<int>(m_volumeFactor * 100.0);
    m_volumeSlider->setValue(value);
}

void NowPlayingHeader::setWaterfallFftSize(int fftSize)
{
    m_preDspWaterfall->setFftSize(fftSize);
    m_postDspWaterfall->setFftSize(fftSize);
}

int NowPlayingHeader::waterfallFftSize() const noexcept
{
    return m_preDspWaterfall ? m_preDspWaterfall->fftSize() : 8192;
}

void NowPlayingHeader::setControlOrder(int order)
{
    if (!m_contentLayout || !m_controlGroup || !m_volumeSlider ||
        !m_coverThumb) {
        return;
    }

    static constexpr std::array<std::array<int, 3>, 6> permutations{{
        {{0, 1, 2}},
        {{0, 2, 1}},
        {{1, 0, 2}},
        {{1, 2, 0}},
        {{2, 0, 1}},
        {{2, 1, 0}}
    }};
    const int normalizedOrder = std::clamp(order, 0, 5);
    const std::array<QWidget*, 3> widgets{
        m_controlGroup, m_volumeSlider, m_coverThumb};

    for (QWidget* widget : widgets) {
        m_contentLayout->removeWidget(widget);
    }
    for (int index = 0; index < 3; ++index) {
        m_contentLayout->insertWidget(
            index,
            widgets[static_cast<std::size_t>(
                permutations[static_cast<std::size_t>(normalizedOrder)]
                            [static_cast<std::size_t>(index)])]);
    }
    m_controlOrder = normalizedOrder;
}

int NowPlayingHeader::controlOrder() const noexcept
{
    return m_controlOrder;
}

void NowPlayingHeader::updateProgress(double currentSec, double totalSec) {
    m_totalDuration = totalSec;
    m_waveform->setDuration(totalSec);
    if (m_hoverPreviewVisible) {
        updateWaveformHoverLabel(true, m_hoverPreviewProgress);
    }
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
