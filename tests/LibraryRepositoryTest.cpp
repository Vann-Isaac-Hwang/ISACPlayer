#include "library/LibraryRepository.h"
#include "library/LibraryScanner.h"
#include "library/PlaybackQueue.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
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
    testPlaybackQueueModes();
    testDirectoriesAndPlaylists(databasePath);
    testBackgroundScanner(
        QDir(temporaryDirectory.path()).filePath("scanner.sqlite"),
        temporaryDirectory.path());
    std::cout << "isac_library_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
