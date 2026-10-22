#pragma once

#include "library/LibraryRepository.h"

#include <QDialog>
#include <QHash>
#include <QListView>
#include <QAbstractListModel>

namespace isac::ui {

class PlaybackQueueListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    explicit PlaybackQueueListModel(library::LibraryRepository* repository,
                                    QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    void setQueue(std::vector<qint64> trackIds, qint64 currentTrackId);
    qint64 trackIdAt(int row) const;

private:
    library::LibraryRepository* m_repository = nullptr;
    std::vector<qint64> m_trackIds;
    qint64 m_currentTrackId = 0;
    mutable QHash<qint64, library::TrackInfo> m_trackCache;
};

class PlaybackQueueView final : public QDialog {
    Q_OBJECT
public:
    explicit PlaybackQueueView(library::LibraryRepository* repository,
                               QWidget* parent = nullptr);

    void setQueue(const std::vector<qint64>& trackIds,
                  qint64 currentTrackId);

signals:
    void trackActivated(qint64 trackId);

private:
    PlaybackQueueListModel* m_model = nullptr;
    QListView* m_listView = nullptr;
};

} // namespace isac::ui
