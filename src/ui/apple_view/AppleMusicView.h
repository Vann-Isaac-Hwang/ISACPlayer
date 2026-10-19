#pragma once
#include <QWidget>
#include <QAbstractTableModel>
#include <QCache>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QIcon>
#include <QSet>
#include <vector>
#include "library/TrackMetadata.h"

namespace isac::ui {

class TrackTableModel final : public QAbstractTableModel {
public:
    explicit TrackTableModel(std::vector<library::TrackInfo>* tracks,
                             const QIcon& defaultIcon,
                             QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void appendTrack(library::TrackInfo track);

private:
    std::vector<library::TrackInfo>* m_tracks = nullptr;
    QIcon m_defaultIcon;
    mutable QCache<int, QIcon> m_coverCache{96};
};

// 1. Apple Music 中央曲库页面
class AppleLibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit AppleLibraryPage(QWidget* parent = nullptr);

    void addTracks(const QStringList& filePaths);
    void updateHeroBanner(const library::TrackInfo& track);
    const std::vector<library::TrackInfo>& tracks() const { return m_tracks; }

signals:
    void trackDoubleClicked(int index);
    void requestImportFiles();
    void requestImportFolder();

private:
    QLabel* m_heroCover;
    QLabel* m_heroTitle;
    QLabel* m_heroArtist;
    QTableView* m_table;
    TrackTableModel* m_tableModel;
    QSet<QString> m_knownPaths;
    std::vector<library::TrackInfo> m_tracks;
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
    QSlider* m_seekSlider;
    QLabel* m_lblCurrentTime;
    QLabel* m_lblTotalTime;
    QSlider* m_volumeSlider;

    double m_totalDuration = 0.0;
    bool m_isUserSeeking = false;
};

} // namespace isac::ui
