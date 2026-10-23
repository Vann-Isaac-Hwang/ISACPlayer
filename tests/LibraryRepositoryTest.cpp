#include "library/LibraryRepository.h"
#include "library/LibraryScanner.h"
#include "library/PlaybackQueue.h"
#include "library/TrackMetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

isac::library::TrackInfo makeTrack(const QString& path,
                                   const QString& title,
                                   const QString& artist,
                                   const QString& album,
                                   qint64 fileSize)
{
    isac::library::TrackInfo track;
    track.filePath = path;
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.durationSeconds = 215;
    track.trackNumber = 3;
    track.year = 2024;
    track.fileSize = fileSize;
    track.modifiedAt = QDateTime::fromString(
        QStringLiteral("2024-01-02T03:04:05.000Z"), Qt::ISODateWithMs);
    return track;
}

void testTrackPersistenceAndQueries(const QString& databasePath)
{
    using namespace isac::library;
    qint64 firstId = 0;
    QDateTime originalAddedAt;

    {
        LibraryRepository repository(databasePath);
        expect(repository.isOpen(), "repository must open SQLite database");

        auto first = makeTrack("/music/one.flac", "First Song", "Artist B",
                               "Album Z", 1200);
        auto second = makeTrack("/music/two.mp3", "Second Song", "Artist A",
                                "Album A", 2400);
        expect(repository.upsertTrack(first), "single track insert must succeed");
        expect(first.id > 0, "insert must return a stable database ID");
        firstId = first.id;
        originalAddedAt = first.addedAt;

        std::vector<TrackInfo> batch{second};
        expect(repository.upsertTracks(batch), "batch track insert must succeed");
        expect(batch.front().id > 0 && batch.front().id != firstId,
               "batch insert must populate a distinct stable ID");
        expect(repository.trackCount() == 2, "track count must reflect inserted tracks");

        const auto byName = repository.queryTracks({}, TrackSort::Title, true);
        expect(byName.size() == 2, "unfiltered query must return both tracks");
        expect(byName[0].title == "First Song",
               "title sort must use the requested ascending order");
        expect(repository.rowForTrackId(firstId, TrackSort::Title, true) == 0 &&
                   repository.rowForTrackId(batch.front().id,
                                            TrackSort::Title, true) == 1,
               "stable row lookup must match the paged query ordering");
        const auto ids = repository.queryTrackIds(TrackSort::Title, true, 1, 1);
        expect(ids.size() == 1 && ids.front() == batch.front().id,
               "track ID pagination must follow the metadata query ordering");

        const auto byArtist = repository.queryTracks(
            "artist a", TrackSort::Artist, true, 10, 0);
        expect(byArtist.size() == 1 && byArtist.front().title == "Second Song",
               "search must match artist case-insensitively");
        expect(repository.queryTracks("%", TrackSort::Title).empty(),
               "search wildcard characters must be treated as literal text");

        const auto page = repository.queryTracks({}, TrackSort::Title, false, 1, 1);
        expect(page.size() == 1 && page.front().title == "First Song",
               "query must support limit and offset");

        auto updated = makeTrack("/music/one.flac", "Renamed Song", "Artist C",
                                 "Album Z", 1800);
        expect(repository.upsertTrack(updated), "same-path upsert must succeed");
        expect(updated.id == firstId, "same path must preserve stable track ID");
        expect(updated.addedAt == originalAddedAt,
               "same-path update must preserve original added time");
        expect(repository.trackCount() == 2, "upsert must not duplicate a track");
    }

    {
        LibraryRepository repository(databasePath);
        expect(repository.isOpen(), "repository must reopen existing database");
        const auto restored = repository.trackById(firstId);
        expect(restored.has_value(), "track must persist after reopening database");
        expect(restored->title == "Renamed Song",
               "updated metadata must persist after reopening");
        expect(restored->fileSize == 1800, "file size metadata must persist");
        expect(restored->trackNumber == 3 && restored->year == 2024,
               "tag fields must persist");
    }
}

void testDirectoriesAndPlaylists(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "repository must open for directory/playlist tests");

    const QString directoryPath = QStringLiteral("/music/library");
    expect(repository.addDirectory(directoryPath), "directory insert must succeed");
    expect(repository.addDirectory(directoryPath), "directory insert must be idempotent");
    const auto scannedAt = QDateTime::currentDateTimeUtc();
    expect(repository.markDirectoryScanned(directoryPath, scannedAt),
           "directory scan timestamp must update");
    const auto directories = repository.directories();
    expect(directories.size() == 1 && directories.front().lastScannedAt.isValid(),
           "directory state must be queryable");
    expect(repository.removeDirectory(directories.front().id),
           "managed directory removal must succeed");
    expect(repository.directories().empty(),
           "removed directory must leave the managed path list");
    expect(repository.addDirectory(directoryPath),
           "directory can be re-added after removal");

    auto first = makeTrack("/music/playlist-a.flac", "Playlist A", "Artist",
                           "Album", 100);
    auto second = makeTrack("/music/playlist-b.flac", "Playlist B", "Artist",
                            "Album", 200);
    std::vector<TrackInfo> tracks{first, second};
    expect(repository.upsertTracks(tracks), "playlist fixtures must insert");

    const auto playlistId = repository.createPlaylist("Road Trip");
    expect(playlistId.has_value(), "playlist creation must return its ID");
    expect(!repository.createPlaylist("   ").has_value(),
           "blank playlist names must be rejected");
    expect(repository.addTrackToPlaylist(*playlistId, tracks[1].id),
           "first playlist track insert must succeed");
    expect(repository.addTrackToPlaylist(*playlistId, tracks[0].id),
           "second playlist track insert must succeed");
    expect(repository.playlistContainsTrack(*playlistId, tracks[0].id) &&
               !repository.playlistContainsTrack(*playlistId, 999999),
           "playlist membership query must identify existing tracks");
    expect(!repository.addTrackToPlaylist(*playlistId, tracks[0].id),
           "duplicate playlist tracks must be rejected");
    const auto batchAdded = repository.addTracksToPlaylist(
        *playlistId, {tracks[0].id, tracks[1].id});
    expect(batchAdded && *batchAdded == 0,
           "batch playlist insertion must skip existing tracks");
    const auto secondPlaylistId = repository.createPlaylist("Batch");
    expect(secondPlaylistId.has_value(),
           "batch playlist must be created");
    const auto insertedBatch = repository.addTracksToPlaylist(
        *secondPlaylistId, {tracks[0].id, tracks[1].id, tracks[0].id});
    expect(insertedBatch && *insertedBatch == 2 &&
               repository.playlistTrackCount(*secondPlaylistId) == 2,
           "batch playlist insertion must add unique tracks in one operation");

    auto playlistTracks = repository.playlistTracks(*playlistId);
    expect(playlistTracks.size() == 2 &&
               playlistTracks[0].id == tracks[1].id &&
               playlistTracks[1].id == tracks[0].id,
           "playlist query must preserve insertion order");
    const auto firstPage = repository.queryPlaylistTracks(*playlistId, {}, 1, 0);
    const auto secondPage = repository.queryPlaylistTracks(*playlistId, {}, 1, 1);
    expect(firstPage.size() == 1 && secondPage.size() == 1 &&
               firstPage.front().id == tracks[1].id &&
               secondPage.front().id == tracks[0].id,
           "playlist pagination must preserve insertion order");
    expect(repository.playlistTrackCount(*playlistId) == 2 &&
               repository.playlistTrackCount(*playlistId, "playlist a") == 1,
           "playlist track count must support search filtering");
    const auto playlistIds =
        repository.playlistTrackIds(*playlistId, {}, 10, 0);
    expect(playlistIds.size() == 2 && playlistIds[0] == tracks[1].id &&
               playlistIds[1] == tracks[0].id,
           "playlist ID query must preserve insertion order for playback");
    expect(repository.playlists().front().trackCount == 2,
           "playlist summary must include track count");

    expect(repository.movePlaylistTrack(*playlistId, tracks[1].id, 1),
           "playlist item must move toward the end");
    playlistTracks = repository.playlistTracks(*playlistId);
    expect(playlistTracks[0].id == tracks[0].id &&
               playlistTracks[1].id == tracks[1].id,
           "forward move must persist the requested playlist order");
    expect(repository.movePlaylistTrack(*playlistId, tracks[0].id, 1),
           "playlist item must move toward the beginning");
    playlistTracks = repository.playlistTracks(*playlistId);
    expect(playlistTracks[0].id == tracks[1].id &&
               playlistTracks[1].id == tracks[0].id,
           "backward move must persist the requested playlist order");
    expect(!repository.movePlaylistTrack(*playlistId, -1, 0),
           "playlist move must reject tracks not in the playlist");

    expect(repository.removeTrackFromPlaylist(*playlistId, tracks[1].id),
           "playlist track removal must succeed");
    playlistTracks = repository.playlistTracks(*playlistId);
    expect(playlistTracks.size() == 1 && playlistTracks.front().id == tracks[0].id,
           "playlist order must remain valid after removal");

    expect(repository.renamePlaylist(*playlistId, "Commute"),
           "playlist rename must succeed");
    expect(!repository.renamePlaylist(*playlistId, "   "),
           "blank playlist rename must be rejected");
    expect(repository.playlists().front().name == "Commute",
           "renamed playlist must be returned");
    expect(repository.deletePlaylist(*playlistId), "playlist deletion must succeed");
    expect(repository.deletePlaylist(*secondPlaylistId),
           "batch playlist deletion must succeed");
    expect(repository.playlists().empty(), "deleted playlist must not remain");
}

void testPersistedPlaybackQueue(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "playback persistence repository must open");
    std::vector<TrackInfo> tracks{
        makeTrack("/queue/one.flac", "One", "Artist", "Album", 100),
        makeTrack("/queue/two.flac", "Two", "Artist", "Album", 100),
        makeTrack("/queue/three.flac", "Three", "Artist", "Album", 100)
    };
    expect(repository.upsertTracks(tracks), "queue fixtures must insert");

    const std::vector<qint64> order{
        tracks[2].id, tracks[0].id, tracks[1].id
    };
    const std::vector<qint64> sourceOrder{
        tracks[0].id, tracks[1].id, tracks[2].id
    };
    expect(repository.savePlaybackQueue(
               order, sourceOrder, 1, PlaybackMode::Shuffle),
           "queue order, cursor and playback mode must persist");
    auto restored = repository.loadPlaybackQueue();
    expect(restored && restored->trackIds == order &&
               restored->sourceTrackIds == sourceOrder &&
               restored->position == 1 &&
               restored->mode == PlaybackMode::Shuffle,
           "queue state must restore exactly after persistence");

    expect(repository.savePlaybackCursor(2, PlaybackMode::RepeatAll),
           "playback cursor and mode must update independently");
    restored = repository.loadPlaybackQueue();
    expect(restored && restored->trackIds == order &&
               restored->position == 2 &&
               restored->mode == PlaybackMode::RepeatAll,
           "cursor update must preserve the stored queue order");

    PlaybackQueue queue;
    queue.restore(restored->trackIds, restored->sourceTrackIds,
                  restored->position, restored->mode);
    expect(queue.currentTrackId() == tracks[1].id &&
               queue.next() == tracks[2].id,
           "restored queue must continue from its saved cursor");
    queue.setMode(PlaybackMode::Sequential);
    expect(queue.orderedTrackIds() == sourceOrder &&
               queue.currentTrackId() == tracks[2].id,
           "switching back to sequential must restore source order at current track");
}

void testPlaybackHistory(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "playback history repository must open");
    std::vector<TrackInfo> tracks{
        makeTrack("/history/one.flac", "History One", "Artist A",
                  "Album A", 100),
        makeTrack("/history/two.flac", "History Two", "Artist B",
                  "Album B", 100)
    };
    expect(repository.upsertTracks(tracks),
           "playback history fixtures must insert");

    expect(repository.recordPlayback(tracks[0].id, 5) &&
               repository.recordPlayback(tracks[1].id, 5) &&
               repository.recordPlayback(tracks[0].id, 5),
           "each successful play must append an independent history entry");
    auto history = repository.queryPlaybackHistory({}, 10, 0);
    expect(history.size() == 3 &&
               history[0].id == tracks[0].id &&
               history[1].id == tracks[1].id &&
               history[2].id == tracks[0].id,
           "history must retain repeated plays in newest-first order");

    for (int i = 0; i < 5; ++i) {
        expect(repository.recordPlayback(tracks[1].id, 5),
               "history append and retention must succeed");
    }
    history = repository.queryPlaybackHistory({}, 10, 0);
    expect(repository.playbackHistoryCount() == 5 &&
               history.size() == 5 &&
               std::all_of(history.begin(), history.end(),
                           [&tracks](const TrackInfo& track) {
                               return track.id == tracks[1].id;
                           }),
           "history retention must keep only the newest configured plays");
    expect(repository.playbackHistoryCount("Artist A") == 0 &&
               repository.playbackHistoryCount("Artist B") == 5,
           "playback history search must filter track metadata");
    expect(repository.trimPlaybackHistory(4) &&
               repository.playbackHistoryCount() == 5,
           "history retention must enforce a minimum of five entries");

    expect(repository.recordPlayback(tracks[0].id, 10, true) &&
               repository.recordPlayback(tracks[1].id, 10, true) &&
               repository.recordPlayback(tracks[0].id, 10, true),
           "latest-only history mode must record repeated plays");
    history = repository.queryPlaybackHistory({}, 10, 0);
    expect(history.size() == 2 &&
               history[0].id == tracks[0].id &&
               history[1].id == tracks[1].id,
           "latest-only mode must replace prior rows for the played track");
    expect(repository.recordPlayback(tracks[0].id, 10, false),
           "disabling latest-only must continue appending history");
    history = repository.queryPlaybackHistory({}, 10, 0);
    expect(history.size() == 3 &&
               history[0].id == tracks[0].id &&
               history[1].id == tracks[0].id,
           "history recorded while disabled must retain repeated entries");
}

void testFilteredSortOrdering(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "sort test repository must open");

    auto first = makeTrack("/sort/first.flac", "Shared Title", "Common Artist",
                           "Album", 100);
    auto second = makeTrack("/sort/second.flac", "shared title", "Common Artist",
                            "Album", 200);
    auto third = makeTrack("/sort/third.flac", "Other Title", "Elsewhere",
                           "Album", 300);
    first.durationSeconds = 310;
    second.durationSeconds = 120;
    third.durationSeconds = 205;
    std::vector<TrackInfo> tracks{first, second, third};
    expect(repository.upsertTracks(tracks), "sort fixtures must insert");

    const auto durationAscending = repository.queryTracks(
        {}, TrackSort::Duration, true, 10, 0);
    const auto durationDescending = repository.queryTracks(
        {}, TrackSort::Duration, false, 10, 0);
    expect(durationAscending.size() == 3 &&
               durationAscending[0].id == tracks[1].id &&
               durationAscending[1].id == tracks[2].id &&
               durationAscending[2].id == tracks[0].id &&
               durationDescending[0].id == tracks[0].id &&
               durationDescending[1].id == tracks[2].id &&
               durationDescending[2].id == tracks[1].id,
           "duration sort must use numeric seconds in both directions");

    for (const bool ascending : {true, false}) {
        const auto sorted = repository.queryTracks(
            {}, TrackSort::Title, ascending, 10, 0);
        const qint64 firstRow = ascending ? 1 : 0;
        expect(sorted.size() == 3 &&
                   sorted[static_cast<std::size_t>(firstRow)].id == tracks[0].id &&
                   sorted[static_cast<std::size_t>(firstRow + 1)].id == tracks[1].id,
               "case-insensitive equal titles must retain ID order in both directions");
        expect(repository.rowForTrackId(
                   tracks[0].id, TrackSort::Title, ascending) == firstRow &&
                   repository.rowForTrackId(
                       tracks[1].id, TrackSort::Title, ascending) == firstRow + 1,
               "stable row lookup must match sorted query tie-breaking");
    }

    expect(repository.trackCount("common") == 2,
           "filtered count must match case-insensitive search results");
    const auto filteredIds = repository.queryTrackIds(
        TrackSort::Title, false, 10, 0, "common");
    expect(filteredIds.size() == 2 &&
               filteredIds[0] == tracks[0].id &&
               filteredIds[1] == tracks[1].id,
           "filtered ID query must preserve descending sort tie-breaking");
    expect(repository.rowForTrackId(
               tracks[1].id, TrackSort::Title, false, "common") == 1 &&
               repository.rowForTrackId(
                   tracks[2].id, TrackSort::Title, false, "common") == -1,
           "filtered stable row lookup must exclude nonmatching tracks");
}

void testArtistAndAlbumQueries(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "category test repository must open");

    std::vector<TrackInfo> tracks{
        makeTrack("/category/a1.flac", "A One", "Artist A / Guest A",
                  "Shared Album", 100),
        makeTrack("/category/a2.flac", "A Two", "artist a / Guest B",
                  "Shared Album", 100),
        makeTrack("/category/a3.flac", "A Three", "Artist A", "Other Album", 100),
        makeTrack("/category/b1.flac", "B One", "Artist B", "Shared Album", 100)
    };
    tracks[0].trackNumber = 2;
    tracks[1].trackNumber = 1;
    tracks[2].trackNumber = 4;
    tracks[3].trackNumber = 3;
    expect(repository.upsertTracks(tracks), "category fixtures must insert");

    const auto artists = repository.queryArtists({}, true, 1, 0);
    expect(repository.artistCount() == 4 && artists.size() == 1 &&
               artists.front().name.compare("Artist A", Qt::CaseInsensitive) == 0 &&
               artists.front().trackCount == 3,
           "artists must split compound artist tags and paginate");
    expect(repository.queryArtists("guest a").size() == 1 &&
               repository.artistTrackCount("Guest A") == 1 &&
               repository.queryArtists("artist b").size() == 1 &&
               repository.artistCount("missing") == 0,
           "artist search must filter category names");

    const auto albums = repository.queryAlbums({}, true, 10, 0);
    expect(repository.albumCount() == 2 && albums.size() == 2 &&
               albums[0].name == "Other Album" &&
               albums[1].name == "Shared Album" &&
               albums[1].artist == "Artist A / Artist B / Guest A / Guest B",
           "albums must merge by title and expose the union of artists");
    expect(repository.albumTrackCount("Shared Album") == 3,
           "album track count must use album identity only");
    const auto albumTracks = repository.queryAlbumTracks(
        "Shared Album", {}, TrackSort::Title, true, 10, 0);
    expect(albumTracks.size() == 3 &&
               albumTracks[0].title == "A One" &&
               albumTracks[1].title == "A Two" &&
               albumTracks[2].title == "B One",
           "album track queries must return every track in the album");
    const auto albumIds = repository.queryAlbumTrackIds(
        "Shared Album", {}, TrackSort::Title, true, 10, 0);
    expect(albumIds.size() == 3 &&
               albumIds[0] == tracks[0].id &&
               albumIds[1] == tracks[1].id &&
               albumIds[2] == tracks[3].id,
           "album ID queries must use album identity only");
    const auto albumTracksByNumber = repository.queryAlbumTracks(
        "Shared Album", {}, TrackSort::TrackNumber, true, 10, 0);
    expect(albumTracksByNumber.size() == 3 &&
               albumTracksByNumber[0].id == tracks[1].id &&
               albumTracksByNumber[1].id == tracks[0].id &&
               albumTracksByNumber[2].id == tracks[3].id,
           "album track queries must support numeric track-number ordering");
    const auto albumIdsByNumber = repository.queryAlbumTrackIds(
        "Shared Album", {}, TrackSort::TrackNumber, false, 2, 0);
    expect(albumIdsByNumber.size() == 2 &&
               albumIdsByNumber[0] == tracks[3].id &&
               albumIdsByNumber[1] == tracks[0].id,
           "album track ID pagination must follow descending track numbers");
    const auto artistIds = repository.queryArtistTrackIds(
        "Artist A", {}, TrackSort::Title, true, 10, 0);
    expect(artistIds.size() == 3 &&
               repository.artistTrackCount("Artist A") == 3,
           "artist playback IDs and count must cover the category tracks");
}

void testLegacyCategoryMigration(const QString& databasePath)
{
    const QString connectionName = QStringLiteral("legacy-fixture-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        expect(database.open(), "legacy fixture database must open");
        QSqlQuery schema(database);
        expect(schema.exec(
                   "CREATE TABLE tracks ("
                   "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, "
                   "title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL, "
                   "duration_seconds INTEGER NOT NULL DEFAULT 0, "
                   "track_number INTEGER NOT NULL DEFAULT 0, "
                   "disc_number INTEGER NOT NULL DEFAULT 0, "
                   "year INTEGER NOT NULL DEFAULT 0, "
                   "file_size INTEGER NOT NULL DEFAULT 0, "
                   "modified_at TEXT, added_at TEXT NOT NULL)"),
               "legacy fixture schema must be created");
        expect(schema.exec(
                   "INSERT INTO tracks "
                   "(path, title, artist, album, added_at) VALUES "
                   "('/legacy/one.flac', 'One', 'Alpha / Beta', 'Legacy Album', "
                   "'2024-01-01T00:00:00.000Z'), "
                   "('/legacy/two.flac', 'Two', 'Gamma', 'Legacy Album', "
                   "'2024-01-01T00:00:00.000Z')"),
               "legacy fixture tracks must be inserted");
        QSqlQuery batchInsert(database);
        batchInsert.prepare(
            "INSERT INTO tracks "
            "(path, title, artist, album, added_at) "
            "VALUES (?, ?, ?, ?, '2024-01-01T00:00:00.000Z')");
        for (int index = 0; index < 513; ++index) {
            batchInsert.addBindValue(
                QStringLiteral("/legacy/batch-%1.flac").arg(index));
            batchInsert.addBindValue(
                QStringLiteral("Batch %1").arg(index));
            batchInsert.addBindValue(
                QStringLiteral("Batch Artist A / Batch Artist B"));
            batchInsert.addBindValue(QStringLiteral("Batch Album"));
            expect(batchInsert.exec(),
                   "legacy batch fixture tracks must be inserted");
            batchInsert.finish();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    isac::library::LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "repository must open legacy database");
    expect(repository.artistCount() == 5 &&
               repository.artistTrackCount("Alpha") == 1 &&
               repository.artistTrackCount("Beta") == 1 &&
               repository.artistTrackCount("Gamma") == 1 &&
               repository.artistTrackCount("Batch Artist A") == 513 &&
               repository.artistTrackCount("Batch Artist B") == 513,
           "legacy compound artists must migrate into separate entities");
    const auto albums = repository.queryAlbums();
    expect(repository.trackCount() == 515 &&
               repository.albumCount() == 2 &&
               albums.size() == 2 &&
               albums.front().name == "Batch Album" &&
               albums.front().artist == "Batch Artist A / Batch Artist B" &&
               albums.front().trackCount == 513 &&
               albums.back().name == "Legacy Album" &&
               albums.back().artist == "Alpha / Beta / Gamma" &&
               albums.back().trackCount == 2,
           "batched legacy migration must preserve album relations and ordering");
}

void testMetadataCategoryNormalization(const QString& databasePath)
{
    using namespace isac::library;
    LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "metadata normalization repository must open");

    std::vector<TrackInfo> tracks{
        makeTrack("/normalization/empty-parts.flac", "Empty Parts",
                  "  Artist One / / Artist Two / artist one  ",
                  "  Shared Name  ", 100),
        makeTrack("/normalization/empty-tags.flac", "Empty Tags",
                  " /  / ", "   ", 100)
    };
    expect(repository.upsertTracks(tracks),
           "metadata normalization fixtures must insert");

    expect(repository.artistCount() == 3 &&
               repository.artistTrackCount("Artist One") == 1 &&
               repository.artistTrackCount("Artist Two") == 1 &&
               repository.artistTrackCount("Unknown Artist") == 1,
           "artist normalization must trim, skip empty parts and deduplicate");
    const auto albums = repository.queryAlbums();
    expect(albums.size() == 2 &&
               albums[0].name == "Shared Name" &&
               albums[1].name == "Unknown Album",
           "album normalization must trim names and replace blank tags");
    expect(albums[0].artist == "Artist One / Artist Two" &&
               albums[1].artist == "Unknown Artist",
           "album artist unions must contain normalized distinct artists");
}

void testPlaybackQueueModes()
{
    using namespace isac::library;
    PlaybackQueue queue;
    queue.reset({11, 22, 33}, 22, PlaybackMode::Sequential);
    expect(queue.currentTrackId() == 22,
           "queue reset must place the selected track at the current position");
    expect(queue.previous() == 11,
           "sequential queue must retain tracks preceding the selected track");
    queue.reset({11, 22, 33}, 22, PlaybackMode::Sequential);
    expect(queue.next() == 33 && !queue.next(),
           "sequential mode must stop at the end of the queue");
    expect(queue.previous() == 22,
           "previous must move through the stable ID queue");

    queue.reset({11, 22, 33}, 22, PlaybackMode::RepeatOne);
    expect(queue.next(true) == 22,
           "natural track end in repeat-one mode must repeat the current track");
    expect(queue.next() == 33,
           "manual next in repeat-one mode must advance to the next track");

    queue.reset({11, 22}, 22, PlaybackMode::RepeatAll);
    expect(queue.next() == 11 && queue.previous() == 22,
           "repeat-all mode must wrap in both directions");

    queue.reset({11, 22, 33}, 11, PlaybackMode::Sequential);
    std::vector<qint64> attempted;
    const bool recovered = queue.advanceToPlayable(
        true, [&attempted](qint64 id) {
            attempted.push_back(id);
            return id == 33;
        });
    expect(recovered && attempted == std::vector<qint64>({22, 33}) &&
               queue.currentTrackId() == 33,
           "automatic advance must skip tracks that fail to load");

    queue.reset({11, 22}, 11, PlaybackMode::Sequential);
    expect(!queue.advanceToPlayable(true, [](qint64) { return false; }),
           "automatic advance must stop after remaining tracks fail");

    queue.reset({11}, 11, PlaybackMode::RepeatAll);
    expect(queue.advanceToPlayable(true, [](qint64 id) { return id == 11; }),
           "repeat-all must restart a single-track queue");

    queue.reset({11, 22, 33, 44}, 33, PlaybackMode::Shuffle);
    expect(queue.currentTrackId() == 33,
           "shuffle must preserve the explicitly selected starting track");
    std::vector<qint64> shuffled;
    for (auto id = queue.currentTrackId(); id; id = queue.next()) {
        shuffled.push_back(*id);
    }
    std::sort(shuffled.begin(), shuffled.end());
    expect(shuffled == std::vector<qint64>({11, 22, 33, 44}),
           "shuffle must visit each queued track exactly once per cycle");
    expect(queue.selectTrack(33),
           "queue must allow selecting a track from the visible order");
    queue.setMode(PlaybackMode::RepeatAll);
    queue.setMode(PlaybackMode::Sequential);
    expect(queue.orderedTrackIds() ==
               std::vector<qint64>({11, 22, 33, 44}) &&
               queue.currentTrackId() == 33,
           "sequential mode must recover canonical order after shuffle");
    queue.setMode(PlaybackMode::Shuffle);
    expect(queue.currentTrackId() == 33 &&
               queue.orderedTrackIds().front() == 33,
           "shuffle mode changes must keep the current track at the head");
}

void writeFixture(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "scanner fixture must be writable");
    expect(file.write(contents) == contents.size(), "scanner fixture must be written");
}

void appendLittleEndian(QByteArray& data, quint32 value, int bytes)
{
    for (int index = 0; index < bytes; ++index) {
        data.append(static_cast<char>((value >> (index * 8)) & 0xff));
    }
}

void testAudioProperties(const QString& directory)
{
    QByteArray wav;
    wav.append("RIFF", 4);
    appendLittleEndian(wav, 42, 4);
    wav.append("WAVEfmt ", 8);
    appendLittleEndian(wav, 16, 4);
    appendLittleEndian(wav, 1, 2);
    appendLittleEndian(wav, 2, 2);
    appendLittleEndian(wav, 44100, 4);
    appendLittleEndian(wav, 264600, 4);
    appendLittleEndian(wav, 6, 2);
    appendLittleEndian(wav, 24, 2);
    wav.append("data", 4);
    appendLittleEndian(wav, 6, 4);
    wav.append(QByteArray(6, '\0'));

    const QString filePath = QDir(directory).filePath("properties.wav");
    writeFixture(filePath, wav);

    isac::library::TrackInfo track;
    track.filePath = filePath;
    isac::library::MetadataReader::readAudioProperties(track);
    expect(track.audioFormat == "WAV",
           "audio properties must include the source file format");
    expect(track.sampleRateHz == 44100 && track.channels == 2 &&
               track.bitDepth == 24,
           "WAV technical properties must be read from the file");
    expect(track.bitrateKbps > 0,
           "audio properties must include bitrate when available");
}

void runScan(isac::library::LibraryScanner& scanner,
             const std::function<void()>& start,
             bool& cancelled,
             QString& error)
{
    QEventLoop loop;
    QTimer timeout;
    bool finished = false;
    timeout.setSingleShot(true);
    QObject::connect(&scanner, &isac::library::LibraryScanner::scanFinished,
                     &loop, [&](bool wasCancelled, const QString& scanError) {
                         cancelled = wasCancelled;
                         error = scanError;
                         finished = true;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    start();
    timeout.start(10000);
    loop.exec();
    expect(finished, "scanner must finish before timeout");
    timeout.stop();
}

void testBackgroundScanner(const QString& databasePath,
                           const QString& fixtureRoot)
{
    using namespace isac::library;
    const QString musicDirectory = QDir(fixtureRoot).filePath("music");
    const QString nestedDirectory = QDir(musicDirectory).filePath("nested");
    expect(QDir().mkpath(nestedDirectory), "scanner fixture directory must exist");
    const QString trackPath = QDir(nestedDirectory).filePath("scanner-track.wav");
    writeFixture(trackPath, QByteArray(128, 'a'));

    LibraryScanner scanner(databasePath);
    int committedTracks = 0;
    QObject::connect(&scanner, &LibraryScanner::batchCommitted,
                     [&committedTracks](int count) { committedTracks += count; });

    bool cancelled = false;
    QString error;
    runScan(scanner, [&] { scanner.scanDirectory(musicDirectory); },
            cancelled, error);
    expect(error.isEmpty() && !cancelled, "initial directory scan must succeed");
    expect(committedTracks == 1, "initial scan must commit the discovered track");
    {
        LibraryRepository repository(databasePath);
        const auto track = repository.trackByPath(trackPath);
        expect(track.has_value(), "directory scan must persist nested audio paths");
        expect(track->title == "scanner-track",
               "scanner must persist fallback metadata when tags are absent");
        expect(repository.directories().size() == 1 &&
                   repository.directories().front().lastScannedAt.isValid(),
               "successful scan must record its directory timestamp");
    }

    committedTracks = 0;
    runScan(scanner, [&] { scanner.scanDirectory(musicDirectory); },
            cancelled, error);
    expect(error.isEmpty() && !cancelled, "unchanged directory scan must succeed");
    expect(committedTracks == 0, "unchanged files must not be rewritten");

    writeFixture(trackPath, QByteArray(256, 'b'));
    committedTracks = 0;
    runScan(scanner, [&] { scanner.scanDirectory(musicDirectory); },
            cancelled, error);
    expect(error.isEmpty() && !cancelled, "modified directory scan must succeed");
    expect(committedTracks == 1, "modified files must be refreshed");
    {
        LibraryRepository repository(databasePath);
        const auto track = repository.trackByPath(trackPath);
        expect(track.has_value() && track->fileSize == 256,
               "modified file metadata must replace the stored record");
        expect(repository.trackCount() == 1,
               "rescanning must update rather than duplicate tracks");
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "temporary test directory must be available");
    const QString databasePath = temporaryDirectory.filePath("library.sqlite");

    testTrackPersistenceAndQueries(databasePath);
    testFilteredSortOrdering(
        QDir(temporaryDirectory.path()).filePath("sort.sqlite"));
    testArtistAndAlbumQueries(
        QDir(temporaryDirectory.path()).filePath("categories.sqlite"));
    testLegacyCategoryMigration(
        QDir(temporaryDirectory.path()).filePath("legacy.sqlite"));
    testMetadataCategoryNormalization(
        QDir(temporaryDirectory.path()).filePath("normalization.sqlite"));
    testPersistedPlaybackQueue(
        QDir(temporaryDirectory.path()).filePath("playback.sqlite"));
    testPlaybackHistory(
        QDir(temporaryDirectory.path()).filePath("history.sqlite"));
    testPlaybackQueueModes();
    testAudioProperties(temporaryDirectory.path());
    testDirectoriesAndPlaylists(databasePath);
    testBackgroundScanner(
        QDir(temporaryDirectory.path()).filePath("scanner.sqlite"),
        temporaryDirectory.path());
    std::cout << "isac_library_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
