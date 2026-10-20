#pragma once
#include <QWidget>
#include <QAbstractTableModel>
#include <QCache>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QIcon>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include <optional>
#include <vector>
#include "library/LibraryRepository.h"
#include "library/TrackMetadata.h"

namespace isac::ui {

class TrackTableModel final : public QAbstractTableModel {
public:
    explicit TrackTableModel(library::LibraryRepository* repository,
                             const QIcon& defaultIcon,
                             QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void refresh();
    void setSearchText(const QString& text);
    void setSort(library::TrackSort sort, bool ascending);
    std::optional<library::TrackInfo> trackAtRow(int row) const;
    int rowForTrackId(qint64 id) const;
    std::vector<qint64> trackIds() const;
    qint64 trackCount() const;

private:
    bool loadPage(int row) const;
    library::LibraryRepository* m_repository = nullptr;
    QIcon m_defaultIcon;
    mutable QCache<int, QIcon> m_coverCache{96};
    mutable std::vector<library::TrackInfo> m_page;
    mutable int m_pageStart = -1;
    qint64 m_trackCount = 0;
    QString m_searchText;
    library::TrackSort m_sort = library::TrackSort::AddedAt;
    bool m_ascending = true;
    static constexpr int kPageSize = 256;
};

// 1. Apple Music 中央曲库页面
class AppleLibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit AppleLibraryPage(library::LibraryRepository* repository,
                              QWidget* parent = nullptr);

    void refreshTracks();
    void setScanStatus(const QString& status);
    void updateHeroBanner(const library::TrackInfo& track);
    std::optional<library::TrackInfo> trackAtRow(int row) const;
    int trackCount() const;
    int rowForTrackId(qint64 id) const;
    std::vector<qint64> trackIds() const;

signals:
    void trackDoubleClicked(qint64 trackId);
    void requestImportFiles();
    void requestImportFolder();

private:
    QLabel* m_heroCover;
    QLabel* m_heroTitle;
    QLabel* m_heroArtist;
    QLabel* m_scanStatus;
    QLabel* m_resultCount;
    QTableView* m_table;
    TrackTableModel* m_tableModel;
    QLineEdit* m_searchInput;
    QComboBox* m_sortSelector;
    QComboBox* m_sortDirection;
    QTimer* m_searchTimer;
};

// 2. 底部常驻 Apple Music 播放控制条
class NowPlayingBar : public QWidget {
    Q_OBJECT
public:
    explicit NowPlayingBar(QWidget* parent = nullptr);

    void setTrackInfo(const library::TrackInfo& info);
    void setPlayingState(bool playing);
    void updateProgress(double currentSec, double totalSec);

signals:
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void playbackModeChanged(int mode);
    void seekRequested(double targetSeconds);
    void volumeChanged(float factor);

private:
    static QString formatTime(double seconds);

    QLabel* m_coverThumb;
    QLabel* m_titleLabel;
    QLabel* m_artistLabel;
    QPushButton* m_btnPrev;
    QPushButton* m_btnPlayPause;
    QPushButton* m_btnNext;
    QComboBox* m_playbackMode;
    QSlider* m_seekSlider;
    QLabel* m_lblCurrentTime;
    QLabel* m_lblTotalTime;
    QSlider* m_volumeSlider;

    double m_totalDuration = 0.0;
    bool m_isUserSeeking = false;
};

} // namespace isac::ui
