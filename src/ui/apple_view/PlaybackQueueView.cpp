#include "PlaybackQueueView.h"

#include <algorithm>
#include <QFont>
#include <QVBoxLayout>

namespace isac::ui {

PlaybackQueueListModel::PlaybackQueueListModel(
    library::LibraryRepository* repository, QObject* parent)
    : QAbstractListModel(parent)
    , m_repository(repository)
{
}

int PlaybackQueueListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_trackIds.size());
}

QVariant PlaybackQueueListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= m_trackIds.size()) {
        return {};
    }

    const qint64 trackId = m_trackIds[static_cast<std::size_t>(index.row())];
    if (role == Qt::FontRole && trackId == m_currentTrackId) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ForegroundRole && trackId == m_currentTrackId) {
        return QColor(QStringLiteral("#FA2D48"));
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    auto track = m_trackCache.constFind(trackId);
    if (track == m_trackCache.cend()) {
        if (!m_repository) {
            return {};
        }
        const auto loaded = m_repository->trackById(trackId);
        if (!loaded) {
            return QStringLiteral("曲目已从资料库移除");
        }
        if (m_trackCache.size() >= 128) {
            m_trackCache.clear();
        }
        track = m_trackCache.insert(trackId, *loaded);
    }
    if (role == Qt::ToolTipRole) {
        return track->album;
    }
    const QString prefix = trackId == m_currentTrackId
        ? QStringLiteral("▶  ") : QString{};
    return QStringLiteral("%1%2  —  %3")
        .arg(prefix, track->title, track->artist);
}

void PlaybackQueueListModel::setQueue(std::vector<qint64> trackIds,
                                      qint64 currentTrackId)
{
    beginResetModel();
    m_trackIds = std::move(trackIds);
    m_currentTrackId = currentTrackId;
    m_trackCache.clear();
    endResetModel();
}

qint64 PlaybackQueueListModel::trackIdAt(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_trackIds.size()) {
        return 0;
    }
    return m_trackIds[static_cast<std::size_t>(row)];
}

PlaybackQueueView::PlaybackQueueView(
    library::LibraryRepository* repository, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("当前播放队列"));
    resize(520, 620);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #181A20; color: #EAEAEA; }"
        "QListView { background: #181A20; color: #EAEAEA; border: none; }"
        "QListView::item { min-height: 34px; padding: 5px 10px; }"
        "QListView::item:selected { background: #343A4B; color: white; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(this);
    m_listView->setUniformItemSizes(true);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model = new PlaybackQueueListModel(repository, this);
    m_listView->setModel(m_model);
    layout->addWidget(m_listView);

    connect(m_listView, &QListView::doubleClicked, this,
            [this](const QModelIndex& index) {
        const qint64 trackId = m_model->trackIdAt(index.row());
        if (trackId > 0) {
            emit trackActivated(trackId);
        }
    });
}

void PlaybackQueueView::setQueue(const std::vector<qint64>& trackIds,
                                 qint64 currentTrackId)
{
    m_model->setQueue(trackIds, currentTrackId);
    setWindowTitle(QStringLiteral("当前播放队列（%1 首）")
                       .arg(trackIds.size()));
    const auto current = std::find(
        trackIds.begin(), trackIds.end(), currentTrackId);
    if (current != trackIds.end()) {
        const int row = static_cast<int>(
            std::distance(trackIds.begin(), current));
        const QModelIndex index = m_model->index(row, 0);
        m_listView->setCurrentIndex(index);
        m_listView->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
}

} // namespace isac::ui
