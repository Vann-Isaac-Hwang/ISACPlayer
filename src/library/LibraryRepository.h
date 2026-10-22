#pragma once

#include "LibraryTypes.h"
#include "PlaybackQueue.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <optional>
#include <vector>

namespace isac::library {

struct LibraryDirectory {
    qint64 id = 0;
    QString path;
    QDateTime lastScannedAt;
};

struct PlaylistInfo {
    qint64 id = 0;
    QString name;
    QDateTime createdAt;
    int trackCount = 0;
};

struct LibraryGroup {
    QString name;
    QString artist;
    qint64 trackCount = 0;
};

struct PersistedPlaybackQueue {
    std::vector<qint64> trackIds;
    std::size_t position = 0;
    PlaybackMode mode = PlaybackMode::Sequential;
};

enum class TrackSort {
    Title,
    Artist,
    Album,
    AddedAt
};

class LibraryRepository {
public:
    // A repository instance is confined to the thread where it was created.
    explicit LibraryRepository(const QString& databasePath);
    ~LibraryRepository();

    LibraryRepository(const LibraryRepository&) = delete;
    LibraryRepository& operator=(const LibraryRepository&) = delete;

    bool isOpen() const;
    QString lastError() const;

    bool upsertTrack(TrackInfo& track);
    bool upsertTracks(std::vector<TrackInfo>& tracks);
    std::optional<TrackInfo> trackById(qint64 id) const;
    std::optional<TrackInfo> trackByPath(const QString& path) const;
    qint64 rowForTrackId(qint64 id, TrackSort sort = TrackSort::AddedAt,
                         bool ascending = true,
                         const QString& searchText = {}) const;
    std::vector<TrackInfo> queryTracks(const QString& searchText = {},
                                       TrackSort sort = TrackSort::AddedAt,
                                       bool ascending = true,
                                       int limit = 200,
                                       int offset = 0) const;
    std::vector<qint64> queryTrackIds(TrackSort sort = TrackSort::AddedAt,
                                      bool ascending = true,
                                      int limit = 1000,
                                      int offset = 0,
                                      const QString& searchText = {}) const;
    qint64 trackCount(const QString& searchText = {}) const;
    std::vector<LibraryGroup> queryArtists(
        const QString& searchText = {}, bool ascending = true,
        int limit = 256, int offset = 0) const;
    qint64 artistCount(const QString& searchText = {}) const;
    std::vector<LibraryGroup> queryAlbums(
        const QString& searchText = {}, bool ascending = true,
        int limit = 256, int offset = 0) const;
    qint64 albumCount(const QString& searchText = {}) const;
    std::vector<TrackInfo> queryArtistTracks(
        const QString& artist, const QString& searchText = {},
        TrackSort sort = TrackSort::Title, bool ascending = true,
        int limit = 256, int offset = 0) const;
    std::vector<qint64> queryArtistTrackIds(
        const QString& artist, const QString& searchText = {},
        TrackSort sort = TrackSort::Title, bool ascending = true,
        int limit = 10000, int offset = 0) const;
    qint64 artistTrackCount(const QString& artist,
                            const QString& searchText = {}) const;
    std::vector<TrackInfo> queryAlbumTracks(
        const QString& album,
        const QString& searchText = {}, TrackSort sort = TrackSort::Title,
        bool ascending = true, int limit = 256, int offset = 0) const;
    std::vector<qint64> queryAlbumTrackIds(
        const QString& album,
        const QString& searchText = {}, TrackSort sort = TrackSort::Title,
        bool ascending = true, int limit = 10000, int offset = 0) const;
    qint64 albumTrackCount(const QString& album,
                           const QString& searchText = {}) const;

    bool addDirectory(const QString& path);
    bool removeDirectory(qint64 directoryId);
    bool markDirectoryScanned(const QString& path, const QDateTime& scannedAt);
    std::vector<LibraryDirectory> directories() const;

    std::optional<qint64> createPlaylist(const QString& name);
    bool renamePlaylist(qint64 playlistId, const QString& name);
    bool deletePlaylist(qint64 playlistId);
    bool addTrackToPlaylist(qint64 playlistId, qint64 trackId);
    std::optional<int> addTracksToPlaylist(
        qint64 playlistId, const std::vector<qint64>& trackIds);
    bool playlistContainsTrack(qint64 playlistId, qint64 trackId) const;
    bool removeTrackFromPlaylist(qint64 playlistId, qint64 trackId);
    bool movePlaylistTrack(qint64 playlistId, qint64 trackId,
                           int destinationIndex);
    std::vector<PlaylistInfo> playlists() const;
    std::vector<TrackInfo> playlistTracks(qint64 playlistId) const;
    std::vector<TrackInfo> queryPlaylistTracks(
        qint64 playlistId, const QString& searchText = {},
        int limit = 256, int offset = 0) const;
    std::vector<qint64> playlistTrackIds(
        qint64 playlistId, const QString& searchText = {},
        int limit = 1000, int offset = 0) const;
    qint64 playlistTrackCount(qint64 playlistId,
                              const QString& searchText = {}) const;
    bool savePlaybackQueue(const std::vector<qint64>& trackIds,
                           std::size_t position, PlaybackMode mode);
    bool savePlaybackCursor(std::size_t position, PlaybackMode mode);
    std::optional<PersistedPlaybackQueue> loadPlaybackQueue() const;

private:
    bool initializeSchema();
    bool syncTrackRelations(qint64 trackId, const QString& artist,
                            const QString& album);
    static TrackInfo readTrackRow(const QSqlQuery& query);

    QString m_connectionName;
    QSqlDatabase m_database;
    mutable QString m_lastError;
};

} // namespace isac::library
