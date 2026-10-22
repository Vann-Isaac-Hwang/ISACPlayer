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
    expect(!repository.addTrackToPlaylist(*playlistId, tracks[0].id),
           "duplicate playlist tracks must be rejected");

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
    expect(repository.savePlaybackQueue(order, 1, PlaybackMode::Shuffle),
           "queue order, cursor and playback mode must persist");
    auto restored = repository.loadPlaybackQueue();
    expect(restored && restored->trackIds == order &&
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
    queue.restore(restored->trackIds, restored->position, restored->mode);
    expect(queue.currentTrackId() == tracks[1].id &&
               queue.next() == tracks[2].id,
           "restored queue must continue from its saved cursor");
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
    std::vector<TrackInfo> tracks{first, second, third};
    expect(repository.upsertTracks(tracks), "sort fixtures must insert");

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
    expect(repository.albumTrackCount("Artist A", "Shared Album") == 3,
           "album track count must use album identity only");
    const auto albumTracks = repository.queryAlbumTracks(
        "Artist A", "Shared Album", {}, TrackSort::Title, true, 10, 0);
    expect(albumTracks.size() == 3 &&
               albumTracks[0].title == "A One" &&
               albumTracks[1].title == "A Two" &&
               albumTracks[2].title == "B One",
           "album track queries must return every track in the album");
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
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    isac::library::LibraryRepository repository(databasePath);
    expect(repository.isOpen(), "repository must open legacy database");
    expect(repository.artistCount() == 3 &&
               repository.artistTrackCount("Alpha") == 1 &&
               repository.artistTrackCount("Beta") == 1 &&
               repository.artistTrackCount("Gamma") == 1,
           "legacy compound artists must migrate into separate entities");
    const auto albums = repository.queryAlbums();
    expect(albums.size() == 1 &&
               albums.front().name == "Legacy Album" &&
               albums.front().artist == "Alpha / Beta / Gamma" &&
               albums.front().trackCount == 2,
           "legacy albums must migrate by name with the artist union");
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
    testPersistedPlaybackQueue(
        QDir(temporaryDirectory.path()).filePath("playback.sqlite"));
    testPlaybackQueueModes();
    testAudioProperties(temporaryDirectory.path());
    testDirectoriesAndPlaylists(databasePath);
    testBackgroundScanner(
        QDir(temporaryDirectory.path()).filePath("scanner.sqlite"),
        temporaryDirectory.path());
    std::cout << "isac_library_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
