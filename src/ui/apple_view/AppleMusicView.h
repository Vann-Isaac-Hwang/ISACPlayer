#pragma once
#include <QWidget>
#include <QAbstractTableModel>
#include <QCache>
#include <QTableView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QIcon>
#include <QListView>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTimer>
#include <QTabBar>
#include <QToolButton>
#include <QMimeData>
#include <optional>
#include <vector>
#include "library/LibraryRepository.h"
#include "library/TrackMetadata.h"
#include "ui/player/AudioVisualization.h"

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
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

    void refresh();
    void setSearchText(const QString& text);
    void setSort(library::TrackSort sort, bool ascending);
    void setPlaylistId(std::optional<qint64> playlistId);
    void setCategoryFilter(const QString& artist, const QString& album);
    void setHistoryMode(bool enabled);
    std::optional<library::TrackInfo> trackAtRow(int row) const;
    int rowForTrackId(qint64 id) const;
    std::vector<qint64> trackIds() const;
    qint64 trackCount() const;
    std::optional<qint64> playlistId() const;

private:
    bool loadPage(int row) const;
    library::LibraryRepository* m_repository = nullptr;
    QIcon m_defaultIcon;
    mutable QCache<int, QIcon> m_coverCache{96};
    mutable std::vector<library::TrackInfo> m_page;
    mutable int m_pageStart = -1;
    qint64 m_trackCount = 0;
    std::optional<qint64> m_playlistId;
    bool m_playlistContext = false;
    bool m_historyContext = false;
    QString m_artistFilter;
    QString m_albumFilter;
    QString m_searchText;
    library::TrackSort m_sort = library::TrackSort::AddedAt;
    bool m_ascending = true;
    static constexpr int kPageSize = 256;
};

class LibraryGroupTableModel final : public QAbstractTableModel {
public:
    explicit LibraryGroupTableModel(library::LibraryRepository* repository,
                                    bool albums,
                                    QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void refresh();
    void setSearchText(const QString& text);
    void setAscending(bool ascending);
    std::optional<library::LibraryGroup> groupAtRow(int row) const;
    qint64 groupCount() const;

private:
    bool loadPage(int row) const;
    library::LibraryRepository* m_repository = nullptr;
    bool m_albums = false;
    QString m_searchText;
    bool m_ascending = true;
    qint64 m_groupCount = 0;
    mutable std::vector<library::LibraryGroup> m_page;
    mutable int m_pageStart = -1;
    mutable QCache<int, QIcon> m_coverCache{64};
    static constexpr int kPageSize = 256;
};

// 1. Apple Music 中央曲库页面
class AppleLibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit AppleLibraryPage(library::LibraryRepository* repository,
                              QWidget* parent = nullptr);

    void refreshTracks();
    void refreshPlaybackHistory();
    void setScanStatus(const QString& status);
    void showArtist(const QString& artist);
    void showAlbum(const QString& album);
    std::optional<library::TrackInfo> trackAtRow(int row) const;
    int trackCount() const;
    int rowForTrackId(qint64 id) const;
    std::vector<qint64> trackIds() const;

signals:
    void trackDoubleClicked(qint64 trackId);

private:
    library::LibraryRepository* m_repository = nullptr;
    QLabel* m_scanStatus;
    QLabel* m_resultCount;
    QTableView* m_table;
    TrackTableModel* m_tableModel;
    LibraryGroupTableModel* m_artistModel;
    LibraryGroupTableModel* m_albumModel;
    QTabBar* m_libraryTabs;
    QPushButton* m_groupBackButton;
    QToolButton* m_albumListButton = nullptr;
    QToolButton* m_albumCoverButton = nullptr;
    QWidget* m_albumViewMode = nullptr;
    QListView* m_albumCoverView = nullptr;
    QStackedWidget* m_libraryContent = nullptr;
    int m_libraryView = 0;
    bool m_showingGroupTracks = false;
    QLineEdit* m_searchInput;
    QTimer* m_searchTimer;
    library::TrackSort m_trackSort = library::TrackSort::AddedAt;
    bool m_trackSortAscending = true;

    void showLibraryView(int view);
    void openGroupAtRow(int row);
    void showGroupTracks(const library::LibraryGroup& group);
    void updateResultCount();
};

class PlaylistPage final : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPage(library::LibraryRepository* repository,
                          QWidget* parent = nullptr);

    void refresh();
    std::vector<qint64> trackIds() const;

signals:
    void trackDoubleClicked(qint64 trackId);

private:
    library::LibraryRepository* m_repository = nullptr;
    QListWidget* m_playlistList = nullptr;
    QTableView* m_table = nullptr;
    TrackTableModel* m_tableModel = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_addTracksButton = nullptr;
    QPushButton* m_removeTracksButton = nullptr;

    std::optional<qint64> selectedPlaylistId() const;
    void refreshPlaylistList(std::optional<qint64> preferred = std::nullopt);
    void updateControls();
    void openAddTracksDialog();
};

class NowPlayingHeader final : public QWidget {
    Q_OBJECT
public:
    explicit NowPlayingHeader(QWidget* parent = nullptr);

    void setTrackInfo(const library::TrackInfo& info);
    void setAudioInfoVisibility(bool format, bool sampleRate, bool bitDepth,
                                bool channels, bool bitrate);
    void setPlayingState(bool playing);
    void setPlaybackMode(int mode);
    void setVolume(double volume);
    void updateProgress(double currentSec, double totalSec);
    void updateVisualization(const double* preLeft, const double* preRight,
                             const double* postLeft, const double* postRight,
                             std::size_t count, std::uint32_t sampleRate);

signals:
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void playbackModeChanged(int mode);
    void seekRequested(double targetSeconds);
    void volumeChanged(float factor);
    void coverContextMenuRequested(qint64 trackId, const QPoint& globalPosition);

private:
    static QString formatTime(double seconds);
    void updateAudioInfoLabel();

    QLabel* m_coverThumb;
    QLabel* m_titleLabel;
    QLabel* m_artistLabel;
    QLabel* m_albumLabel;
    QLabel* m_audioInfoLabel;
    QLabel* m_lblCurrentTime;
    QLabel* m_lblTotalTime;
    QPushButton* m_btnPrev;
    QPushButton* m_btnPlayPause;
    QPushButton* m_btnNext;
    QPushButton* m_playbackModeButton;
    WaveformProgressWidget* m_waveform = nullptr;
    WaterfallWidget* m_preDspWaterfall = nullptr;
    WaterfallWidget* m_postDspWaterfall = nullptr;
    QSlider* m_volumeSlider;

    std::vector<double> m_waveformSamples;
    int m_playbackModeValue = 0;
    double m_totalDuration = 0.0;
    library::TrackInfo m_trackInfo;
    bool m_showAudioFormat = true;
    bool m_showSampleRate = true;
    bool m_showBitDepth = true;
    bool m_showChannels = true;
    bool m_showBitrate = true;
};

} // namespace isac::ui
