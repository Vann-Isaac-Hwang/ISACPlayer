#include "LibraryRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <tuple>

namespace isac::library {
namespace {

QStringList splitArtists(const QString& artistText)
{
    QStringList result;
    QSet<QString> seen;
    const QStringList parts = artistText.split(
        QRegularExpression(QStringLiteral("\\s*/\\s*")),
        Qt::SkipEmptyParts);
    for (QString artist : parts) {
        artist = artist.trimmed();
        if (artist.isEmpty()) {
            continue;
        }
        const QString key = artist.toCaseFolded();
        if (!seen.contains(key)) {
            seen.insert(key);
            result.push_back(std::move(artist));
        }
    }
    if (result.isEmpty()) {
        result.push_back(QStringLiteral("Unknown Artist"));
    }
    return result;
}

QString normalizedAlbum(const QString& album)
{
    const QString trimmed = album.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Unknown Album") : trimmed;
}

QString sortColumn(TrackSort sort)
{
    switch (sort) {
    case TrackSort::Title: return QStringLiteral("title");
    case TrackSort::Artist: return QStringLiteral("artist");
    case TrackSort::Album: return QStringLiteral("album");
    case TrackSort::Duration: return QStringLiteral("duration_seconds");
    case TrackSort::AddedAt: return QStringLiteral("added_at");
    }
    return QStringLiteral("added_at");
}

QString sortExpression(TrackSort sort)
{
    const QString column = sortColumn(sort);
    const bool textSort = sort == TrackSort::Title ||
                          sort == TrackSort::Artist ||
                          sort == TrackSort::Album;
    return textSort
        ? column + QStringLiteral(" COLLATE NOCASE")
        : column;
}

QString escapedSearchPattern(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QStringLiteral("%") + escaped + QStringLiteral("%");
}

constexpr int kRelationMigrationBatchSize = 512;

} // namespace

LibraryRepository::LibraryRepository(const QString& databasePath)
    : m_connectionName(QStringLiteral("isac-library-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName))
{
    m_database.setDatabaseName(databasePath);
    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        return;
    }
    if (!initializeSchema()) {
        m_database.close();
    }
}

LibraryRepository::~LibraryRepository()
{
    if (m_database.isValid()) {
        m_database.close();
    }
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool LibraryRepository::isOpen() const
{
    return m_database.isOpen();
}

QString LibraryRepository::lastError() const
{
    return m_lastError;
}

bool LibraryRepository::initializeSchema()
{
    const QStringList statements{
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tracks ("
            "id INTEGER PRIMARY KEY, "
            "path TEXT NOT NULL UNIQUE, "
            "title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL, "
            "duration_seconds INTEGER NOT NULL DEFAULT 0, "
            "track_number INTEGER NOT NULL DEFAULT 0, "
            "disc_number INTEGER NOT NULL DEFAULT 0, "
            "year INTEGER NOT NULL DEFAULT 0, "
            "file_size INTEGER NOT NULL DEFAULT 0, "
            "modified_at TEXT, added_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title COLLATE NOCASE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist COLLATE NOCASE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album COLLATE NOCASE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_added_at ON tracks(added_at)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS artists ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL COLLATE NOCASE UNIQUE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS track_artists ("
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
            "artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE, "
            "PRIMARY KEY (track_id, artist_id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS albums ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL COLLATE NOCASE UNIQUE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS track_albums ("
            "track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE, "
            "album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_track_artists_artist ON track_artists(artist_id, track_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_track_albums_album ON track_albums(album_id, track_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS library_schema_meta ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS library_directories ("
            "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, last_scanned_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS playlists ("
            "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS playlist_tracks ("
            "playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, "
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
            "position INTEGER NOT NULL, "
            "PRIMARY KEY (playlist_id, track_id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS playback_queue ("
            "position INTEGER PRIMARY KEY, "
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS playback_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
            "played_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_playback_history_track "
            "ON playback_history(track_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS playback_state ("
            "singleton INTEGER PRIMARY KEY CHECK (singleton = 1), "
            "current_track_id INTEGER NOT NULL DEFAULT 0, "
            "position INTEGER NOT NULL DEFAULT 0, "
            "mode INTEGER NOT NULL DEFAULT 0, "
            "updated_at TEXT NOT NULL)")
    };

    for (const QString& statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }

    QSqlQuery migrationState(m_database);
    migrationState.prepare(QStringLiteral(
        "SELECT value FROM library_schema_meta "
        "WHERE key = 'normalized_artist_album_relations'"));
    if (!migrationState.exec()) {
        m_lastError = migrationState.lastError().text();
        return false;
    }
    if (migrationState.next() &&
        migrationState.value(0).toString() == QStringLiteral("1")) {
        return true;
    }

    qint64 lastTrackId = 0;
    while (true) {
        std::vector<std::tuple<qint64, QString, QString>> relations;
        relations.reserve(kRelationMigrationBatchSize);

        QSqlQuery existingTracks(m_database);
        existingTracks.prepare(QStringLiteral(
            "SELECT id, artist, album FROM tracks "
            "WHERE id > ? ORDER BY id ASC LIMIT ?"));
        existingTracks.addBindValue(lastTrackId);
        existingTracks.addBindValue(kRelationMigrationBatchSize);
        if (!existingTracks.exec()) {
            m_lastError = existingTracks.lastError().text();
            return false;
        }
        while (existingTracks.next()) {
            relations.emplace_back(
                existingTracks.value(0).toLongLong(),
                existingTracks.value(1).toString(),
                existingTracks.value(2).toString());
        }
        existingTracks.finish();
        if (relations.empty()) {
            break;
        }

        if (!m_database.transaction()) {
            m_lastError = m_database.lastError().text();
            return false;
        }
        for (const auto& [trackId, artist, album] : relations) {
            if (!syncTrackRelations(trackId, artist, album)) {
                m_database.rollback();
                return false;
            }
        }
        if (!m_database.commit()) {
            m_lastError = m_database.lastError().text();
            m_database.rollback();
            return false;
        }
        lastTrackId = std::get<0>(relations.back());
    }

    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }
    QSqlQuery markMigration(m_database);
    markMigration.prepare(QStringLiteral(
        "INSERT INTO library_schema_meta(key, value) "
        "VALUES ('normalized_artist_album_relations', '1') "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    if (!markMigration.exec()) {
        m_lastError = markMigration.lastError().text();
        m_database.rollback();
        return false;
    }
    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool LibraryRepository::upsertTrack(TrackInfo& track)
{
    std::vector<TrackInfo> tracks{track};
    if (!upsertTracks(tracks)) {
        return false;
    }
    track = std::move(tracks.front());
    return true;
}

bool LibraryRepository::upsertTracks(std::vector<TrackInfo>& tracks)
{
    if (!m_database.isOpen() || !m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO tracks "
        "(path, title, artist, album, duration_seconds, track_number, disc_number, "
        "year, file_size, modified_at, added_at) "
        "VALUES (:path, :title, :artist, :album, :duration, :track, :disc, "
        ":year, :size, :modified, :added) "
        "ON CONFLICT(path) DO UPDATE SET "
        "title=excluded.title, artist=excluded.artist, album=excluded.album, "
        "duration_seconds=excluded.duration_seconds, track_number=excluded.track_number, "
        "disc_number=excluded.disc_number, year=excluded.year, "
        "file_size=excluded.file_size, modified_at=excluded.modified_at"));

    for (TrackInfo& track : tracks) {
        const QDateTime addedAt = track.addedAt.isValid()
            ? track.addedAt : QDateTime::currentDateTimeUtc();
        query.bindValue(QStringLiteral(":path"), track.filePath);
        query.bindValue(QStringLiteral(":title"), track.title);
        query.bindValue(QStringLiteral(":artist"), track.artist);
        query.bindValue(QStringLiteral(":album"), track.album);
        query.bindValue(QStringLiteral(":duration"), std::max(0, track.durationSeconds));
        query.bindValue(QStringLiteral(":track"), std::max(0, track.trackNumber));
        query.bindValue(QStringLiteral(":disc"), std::max(0, track.discNumber));
        query.bindValue(QStringLiteral(":year"), std::max(0, track.year));
        query.bindValue(QStringLiteral(":size"), std::max<qint64>(0, track.fileSize));
        query.bindValue(QStringLiteral(":modified"),
                        track.modifiedAt.isValid()
                            ? track.modifiedAt.toUTC().toString(Qt::ISODateWithMs)
                            : QString{});
        query.bindValue(QStringLiteral(":added"),
                        addedAt.toUTC().toString(Qt::ISODateWithMs));
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }

        QSqlQuery idQuery(m_database);
        idQuery.prepare(QStringLiteral("SELECT id, added_at FROM tracks WHERE path = ?"));
        idQuery.addBindValue(track.filePath);
        if (!idQuery.exec() || !idQuery.next()) {
            m_lastError = idQuery.lastError().text();
            m_database.rollback();
            return false;
        }
        track.id = idQuery.value(0).toLongLong();
        track.addedAt = QDateTime::fromString(idQuery.value(1).toString(),
                                              Qt::ISODateWithMs);
        if (!syncTrackRelations(track.id, track.artist, track.album)) {
            m_database.rollback();
            return false;
        }
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool LibraryRepository::syncTrackRelations(qint64 trackId,
                                           const QString& artist,
                                           const QString& album)
{
    QSqlQuery clearArtists(m_database);
    clearArtists.prepare(QStringLiteral(
        "DELETE FROM track_artists WHERE track_id = ?"));
    clearArtists.addBindValue(trackId);
    if (!clearArtists.exec()) {
        m_lastError = clearArtists.lastError().text();
        return false;
    }

    for (const QString& artistName : splitArtists(artist)) {
        QSqlQuery insertArtist(m_database);
        insertArtist.prepare(QStringLiteral(
            "INSERT INTO artists(name) VALUES (?) "
            "ON CONFLICT(name) DO NOTHING"));
        insertArtist.addBindValue(artistName);
        if (!insertArtist.exec()) {
            m_lastError = insertArtist.lastError().text();
            return false;
        }

        QSqlQuery artistId(m_database);
        artistId.prepare(QStringLiteral(
            "SELECT id FROM artists WHERE name = ? COLLATE NOCASE"));
        artistId.addBindValue(artistName);
        if (!artistId.exec() || !artistId.next()) {
            m_lastError = artistId.lastError().text();
            return false;
        }

        QSqlQuery link(m_database);
        link.prepare(QStringLiteral(
            "INSERT INTO track_artists(track_id, artist_id) VALUES (?, ?)"));
        link.addBindValue(trackId);
        link.addBindValue(artistId.value(0).toLongLong());
        if (!link.exec()) {
            m_lastError = link.lastError().text();
            return false;
        }
    }

    QSqlQuery clearAlbum(m_database);
    clearAlbum.prepare(QStringLiteral(
        "DELETE FROM track_albums WHERE track_id = ?"));
    clearAlbum.addBindValue(trackId);
    if (!clearAlbum.exec()) {
        m_lastError = clearAlbum.lastError().text();
        return false;
    }

    const QString albumName = normalizedAlbum(album);
    QSqlQuery insertAlbum(m_database);
    insertAlbum.prepare(QStringLiteral(
        "INSERT INTO albums(name) VALUES (?) "
        "ON CONFLICT(name) DO NOTHING"));
    insertAlbum.addBindValue(albumName);
    if (!insertAlbum.exec()) {
        m_lastError = insertAlbum.lastError().text();
        return false;
    }

    QSqlQuery albumId(m_database);
    albumId.prepare(QStringLiteral(
        "SELECT id FROM albums WHERE name = ? COLLATE NOCASE"));
    albumId.addBindValue(albumName);
    if (!albumId.exec() || !albumId.next()) {
        m_lastError = albumId.lastError().text();
        return false;
    }

    QSqlQuery albumLink(m_database);
    albumLink.prepare(QStringLiteral(
        "INSERT INTO track_albums(track_id, album_id) VALUES (?, ?)"));
    albumLink.addBindValue(trackId);
    albumLink.addBindValue(albumId.value(0).toLongLong());
    if (!albumLink.exec()) {
        m_lastError = albumLink.lastError().text();
        return false;
    }
    return true;
}

TrackInfo LibraryRepository::readTrackRow(const QSqlQuery& query)
{
    TrackInfo track;
    track.id = query.value(QStringLiteral("id")).toLongLong();
    track.filePath = query.value(QStringLiteral("path")).toString();
    track.title = query.value(QStringLiteral("title")).toString();
    track.artist = query.value(QStringLiteral("artist")).toString();
    track.album = query.value(QStringLiteral("album")).toString();
    track.durationSeconds = query.value(QStringLiteral("duration_seconds")).toInt();
    track.trackNumber = query.value(QStringLiteral("track_number")).toInt();
    track.discNumber = query.value(QStringLiteral("disc_number")).toInt();
    track.year = query.value(QStringLiteral("year")).toInt();
    track.fileSize = query.value(QStringLiteral("file_size")).toLongLong();
    track.modifiedAt = QDateTime::fromString(
        query.value(QStringLiteral("modified_at")).toString(), Qt::ISODateWithMs);
    track.addedAt = QDateTime::fromString(
        query.value(QStringLiteral("added_at")).toString(), Qt::ISODateWithMs);
    return track;
}

std::optional<TrackInfo> LibraryRepository::trackById(qint64 id) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT * FROM tracks WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return readTrackRow(query);
}

std::optional<TrackInfo> LibraryRepository::trackByPath(const QString& path) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT * FROM tracks WHERE path = ?"));
    query.addBindValue(path);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return readTrackRow(query);
}

qint64 LibraryRepository::rowForTrackId(qint64 id, TrackSort sort,
                                        bool ascending,
                                        const QString& searchText) const
{
    const QString candidateExpression =
        QStringLiteral("candidate.") + sortExpression(sort);
    const QString selectedExpression =
        QStringLiteral("selected.") + sortExpression(sort);
    const QString comparison = ascending ? QStringLiteral("<")
                                         : QStringLiteral(">");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM tracks AS candidate "
        "JOIN tracks AS selected ON selected.id = ? "
        "WHERE (? = '' OR candidate.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR candidate.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR candidate.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "AND (? = '' OR selected.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR selected.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR selected.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "AND (%1 %2 %3 OR (%1 = %3 AND candidate.id < selected.id))")
                      .arg(candidateExpression, comparison, selectedExpression));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(id);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    const qint64 row = query.value(0).toLongLong();
    QSqlQuery existsQuery(m_database);
    existsQuery.prepare(QStringLiteral(
        "SELECT 1 FROM tracks "
        "WHERE id = ? AND (? = '' OR title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    existsQuery.addBindValue(id);
    existsQuery.addBindValue(searchText);
    existsQuery.addBindValue(pattern);
    existsQuery.addBindValue(pattern);
    existsQuery.addBindValue(pattern);
    if (!existsQuery.exec() || !existsQuery.next()) {
        return -1;
    }
    return row;
}

std::vector<TrackInfo> LibraryRepository::queryTracks(
    const QString& searchText, TrackSort sort, bool ascending,
    int limit, int offset) const
{
    std::vector<TrackInfo> tracks;
    const QString direction = ascending ? QStringLiteral("ASC") : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT * FROM tracks "
        "WHERE (? = '' OR title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY %1 %2, id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return tracks;
    }
    while (query.next()) {
        tracks.push_back(readTrackRow(query));
    }
    return tracks;
}

std::vector<qint64> LibraryRepository::queryTrackIds(
    TrackSort sort, bool ascending, int limit, int offset,
    const QString& searchText) const
{
    std::vector<qint64> ids;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id FROM tracks "
        "WHERE (? = '' OR title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY %1 %2, id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return ids;
    }
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return ids;
}

qint64 LibraryRepository::trackCount(const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM tracks "
        "WHERE (? = '' OR title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

std::vector<LibraryGroup> LibraryRepository::queryArtists(
    const QString& searchText, bool ascending, int limit, int offset) const
{
    std::vector<LibraryGroup> groups;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT a.name, '', COUNT(DISTINCT ta.track_id) "
        "FROM artists AS a "
        "JOIN track_artists AS ta ON ta.artist_id = a.id "
        "WHERE (? = '' OR a.name LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "GROUP BY a.id, a.name "
        "ORDER BY a.name COLLATE NOCASE %1 LIMIT ? OFFSET ?").arg(direction));
    query.addBindValue(searchText);
    query.addBindValue(escapedSearchPattern(searchText));
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return groups;
    }
    while (query.next()) {
        groups.push_back({query.value(0).toString(), {}, query.value(2).toLongLong()});
    }
    return groups;
}

qint64 LibraryRepository::artistCount(const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM artists AS a "
        "WHERE (? = '' OR a.name LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "AND EXISTS ("
        "SELECT 1 FROM track_artists AS ta WHERE ta.artist_id = a.id)"));
    query.addBindValue(searchText);
    query.addBindValue(escapedSearchPattern(searchText));
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

std::vector<LibraryGroup> LibraryRepository::queryAlbums(
    const QString& searchText, bool ascending, int limit, int offset) const
{
    std::vector<LibraryGroup> groups;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT al.id, al.name, COUNT(DISTINCT tal.track_id) "
        "FROM albums AS al "
        "JOIN track_albums AS tal ON tal.album_id = al.id "
        "WHERE (? = '' OR al.name LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR EXISTS ("
        "SELECT 1 FROM track_albums AS search_tal "
        "JOIN track_artists AS search_ta "
        "ON search_ta.track_id = search_tal.track_id "
        "JOIN artists AS search_a ON search_a.id = search_ta.artist_id "
        "WHERE search_tal.album_id = al.id "
        "AND search_a.name LIKE ? ESCAPE '\\' COLLATE NOCASE)) "
        "GROUP BY al.id, al.name "
        "ORDER BY al.name COLLATE NOCASE %1 "
        "LIMIT ? OFFSET ?").arg(direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return groups;
    }
    while (query.next()) {
        QStringList artists;
        QSqlQuery artistQuery(m_database);
        artistQuery.prepare(QStringLiteral(
            "SELECT DISTINCT a.name "
            "FROM artists AS a "
            "JOIN track_artists AS ta ON ta.artist_id = a.id "
            "JOIN track_albums AS tal ON tal.track_id = ta.track_id "
            "WHERE tal.album_id = ? "
            "ORDER BY a.name COLLATE NOCASE"));
        artistQuery.addBindValue(query.value(0).toLongLong());
        if (!artistQuery.exec()) {
            m_lastError = artistQuery.lastError().text();
            return {};
        }
        while (artistQuery.next()) {
            artists.push_back(artistQuery.value(0).toString());
        }
        groups.push_back({
            query.value(1).toString(), artists.join(QStringLiteral(" / ")),
            query.value(2).toLongLong()
        });
    }
    return groups;
}

qint64 LibraryRepository::albumCount(const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM ("
        "SELECT al.id FROM albums AS al "
        "JOIN track_albums AS tal ON tal.album_id = al.id "
        "WHERE (? = '' OR al.name LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR EXISTS ("
        "SELECT 1 FROM track_albums AS search_tal "
        "JOIN track_artists AS search_ta "
        "ON search_ta.track_id = search_tal.track_id "
        "JOIN artists AS search_a ON search_a.id = search_ta.artist_id "
        "WHERE search_tal.album_id = al.id "
        "AND search_a.name LIKE ? ESCAPE '\\' COLLATE NOCASE)) "
        "GROUP BY al.id)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

std::vector<TrackInfo> LibraryRepository::queryArtistTracks(
    const QString& artist, const QString& searchText, TrackSort sort,
    bool ascending, int limit, int offset) const
{
    std::vector<TrackInfo> tracks;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.* FROM tracks AS t "
        "JOIN track_artists AS ta ON ta.track_id = t.id "
        "JOIN artists AS a ON a.id = ta.artist_id "
        "WHERE a.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY t.%1 %2, t.id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(artist);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return tracks;
    }
    while (query.next()) {
        tracks.push_back(readTrackRow(query));
    }
    return tracks;
}

std::vector<qint64> LibraryRepository::queryArtistTrackIds(
    const QString& artist, const QString& searchText, TrackSort sort,
    bool ascending, int limit, int offset) const
{
    std::vector<qint64> ids;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.id FROM tracks AS t "
        "JOIN track_artists AS ta ON ta.track_id = t.id "
        "JOIN artists AS a ON a.id = ta.artist_id "
        "WHERE a.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY t.%1 %2, t.id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(artist);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return ids;
    }
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return ids;
}

qint64 LibraryRepository::artistTrackCount(
    const QString& artist, const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT t.id) FROM tracks AS t "
        "JOIN track_artists AS ta ON ta.track_id = t.id "
        "JOIN artists AS a ON a.id = ta.artist_id "
        "WHERE a.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(artist);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

std::vector<TrackInfo> LibraryRepository::queryAlbumTracks(
    const QString& album, const QString& searchText,
    TrackSort sort, bool ascending, int limit, int offset) const
{
    std::vector<TrackInfo> tracks;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.* FROM tracks AS t "
        "JOIN track_albums AS ta ON ta.track_id = t.id "
        "JOIN albums AS al ON al.id = ta.album_id "
        "WHERE al.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY t.%1 %2, t.id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(album);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return tracks;
    }
    while (query.next()) {
        tracks.push_back(readTrackRow(query));
    }
    return tracks;
}

std::vector<qint64> LibraryRepository::queryAlbumTrackIds(
    const QString& album, const QString& searchText,
    TrackSort sort, bool ascending, int limit, int offset) const
{
    std::vector<qint64> ids;
    const QString direction = ascending ? QStringLiteral("ASC")
                                       : QStringLiteral("DESC");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.id FROM tracks AS t "
        "JOIN track_albums AS ta ON ta.track_id = t.id "
        "JOIN albums AS al ON al.id = ta.album_id "
        "WHERE al.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY t.%1 %2, t.id ASC LIMIT ? OFFSET ?")
                      .arg(sortExpression(sort), direction));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(album);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return ids;
    }
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return ids;
}

qint64 LibraryRepository::albumTrackCount(
    const QString& album, const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT t.id) FROM tracks AS t "
        "JOIN track_albums AS ta ON ta.track_id = t.id "
        "JOIN albums AS al ON al.id = ta.album_id "
        "WHERE al.name = ? COLLATE NOCASE "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(album);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

bool LibraryRepository::addDirectory(const QString& path)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO library_directories(path) VALUES (?) "
        "ON CONFLICT(path) DO NOTHING"));
    query.addBindValue(path);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool LibraryRepository::removeDirectory(qint64 directoryId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM library_directories WHERE id = ?"));
    query.addBindValue(directoryId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool LibraryRepository::markDirectoryScanned(const QString& path,
                                             const QDateTime& scannedAt)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE library_directories SET last_scanned_at = ? WHERE path = ?"));
    query.addBindValue(scannedAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(path);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

std::vector<LibraryDirectory> LibraryRepository::directories() const
{
    std::vector<LibraryDirectory> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT id, path, last_scanned_at FROM library_directories ORDER BY path"))) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.push_back({
            query.value(0).toLongLong(),
            query.value(1).toString(),
            QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs)
        });
    }
    return result;
}

std::optional<qint64> LibraryRepository::createPlaylist(const QString& name)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_lastError = QStringLiteral("Playlist name cannot be empty");
        return std::nullopt;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO playlists(name, created_at) VALUES (?, ?)"));
    query.addBindValue(trimmedName);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return std::nullopt;
    }
    return query.lastInsertId().toLongLong();
}

bool LibraryRepository::renamePlaylist(qint64 playlistId, const QString& name)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_lastError = QStringLiteral("Playlist name cannot be empty");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE playlists SET name = ? WHERE id = ?"));
    query.addBindValue(trimmedName);
    query.addBindValue(playlistId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool LibraryRepository::deletePlaylist(qint64 playlistId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE id = ?"));
    query.addBindValue(playlistId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool LibraryRepository::addTrackToPlaylist(qint64 playlistId, qint64 trackId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO playlist_tracks(playlist_id, track_id, position) "
        "SELECT ?, ?, COALESCE(MAX(position) + 1, 0) "
        "FROM playlist_tracks WHERE playlist_id = ?"));
    query.addBindValue(playlistId);
    query.addBindValue(trackId);
    query.addBindValue(playlistId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

std::optional<int> LibraryRepository::addTracksToPlaylist(
    qint64 playlistId, const std::vector<qint64>& trackIds)
{
    if (playlistId <= 0 || !m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return std::nullopt;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO playlist_tracks(playlist_id, track_id, position) "
        "SELECT ?, ?, COALESCE(MAX(position) + 1, 0) "
        "FROM playlist_tracks WHERE playlist_id = ?"));
    int insertedCount = 0;
    for (const qint64 trackId : trackIds) {
        if (trackId <= 0) {
            m_lastError = QStringLiteral("Invalid track ID");
            m_database.rollback();
            return std::nullopt;
        }
        query.bindValue(0, playlistId);
        query.bindValue(1, trackId);
        query.bindValue(2, playlistId);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return std::nullopt;
        }
        insertedCount += static_cast<int>(query.numRowsAffected() == 1);
        query.finish();
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        return std::nullopt;
    }
    return insertedCount;
}

bool LibraryRepository::playlistContainsTrack(
    qint64 playlistId, qint64 trackId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM playlist_tracks "
        "WHERE playlist_id = ? AND track_id = ? LIMIT 1"));
    query.addBindValue(playlistId);
    query.addBindValue(trackId);
    return query.exec() && query.next();
}

bool LibraryRepository::removeTrackFromPlaylist(qint64 playlistId, qint64 trackId)
{
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    QSqlQuery findQuery(m_database);
    findQuery.prepare(QStringLiteral(
        "SELECT position FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?"));
    findQuery.addBindValue(playlistId);
    findQuery.addBindValue(trackId);
    if (!findQuery.exec() || !findQuery.next()) {
        m_lastError = findQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    const int removedPosition = findQuery.value(0).toInt();

    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare(QStringLiteral(
        "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?"));
    deleteQuery.addBindValue(playlistId);
    deleteQuery.addBindValue(trackId);
    if (!deleteQuery.exec()) {
        m_lastError = deleteQuery.lastError().text();
        m_database.rollback();
        return false;
    }

    QSqlQuery shiftQuery(m_database);
    shiftQuery.prepare(QStringLiteral(
        "UPDATE playlist_tracks SET position = position - 1 "
        "WHERE playlist_id = ? AND position > ?"));
    shiftQuery.addBindValue(playlistId);
    shiftQuery.addBindValue(removedPosition);
    if (!shiftQuery.exec() || !m_database.commit()) {
        m_lastError = shiftQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool LibraryRepository::movePlaylistTrack(qint64 playlistId, qint64 trackId,
                                         int destinationIndex)
{
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    QSqlQuery positionQuery(m_database);
    positionQuery.prepare(QStringLiteral(
        "SELECT position FROM playlist_tracks "
        "WHERE playlist_id = ? AND track_id = ?"));
    positionQuery.addBindValue(playlistId);
    positionQuery.addBindValue(trackId);
    if (!positionQuery.exec() || !positionQuery.next()) {
        m_lastError = positionQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    const int sourceIndex = positionQuery.value(0).toInt();

    QSqlQuery countQuery(m_database);
    countQuery.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = ?"));
    countQuery.addBindValue(playlistId);
    if (!countQuery.exec() || !countQuery.next()) {
        m_lastError = countQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    const int trackCount = countQuery.value(0).toInt();
    if (trackCount <= 0) {
        m_database.rollback();
        return false;
    }
    const int targetIndex = std::clamp(destinationIndex, 0, trackCount - 1);
    if (sourceIndex == targetIndex) {
        if (!m_database.commit()) {
            m_lastError = m_database.lastError().text();
            m_database.rollback();
            return false;
        }
        return true;
    }

    QSqlQuery shiftQuery(m_database);
    if (sourceIndex < targetIndex) {
        shiftQuery.prepare(QStringLiteral(
            "UPDATE playlist_tracks SET position = position - 1 "
            "WHERE playlist_id = ? AND position > ? AND position <= ?"));
        shiftQuery.addBindValue(playlistId);
        shiftQuery.addBindValue(sourceIndex);
        shiftQuery.addBindValue(targetIndex);
    } else {
        shiftQuery.prepare(QStringLiteral(
            "UPDATE playlist_tracks SET position = position + 1 "
            "WHERE playlist_id = ? AND position >= ? AND position < ?"));
        shiftQuery.addBindValue(playlistId);
        shiftQuery.addBindValue(targetIndex);
        shiftQuery.addBindValue(sourceIndex);
    }
    if (!shiftQuery.exec()) {
        m_lastError = shiftQuery.lastError().text();
        m_database.rollback();
        return false;
    }

    QSqlQuery moveQuery(m_database);
    moveQuery.prepare(QStringLiteral(
        "UPDATE playlist_tracks SET position = ? "
        "WHERE playlist_id = ? AND track_id = ?"));
    moveQuery.addBindValue(targetIndex);
    moveQuery.addBindValue(playlistId);
    moveQuery.addBindValue(trackId);
    if (!moveQuery.exec() || !m_database.commit()) {
        m_lastError = moveQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

std::vector<PlaylistInfo> LibraryRepository::playlists() const
{
    std::vector<PlaylistInfo> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT p.id, p.name, p.created_at, COUNT(pt.track_id) "
            "FROM playlists p LEFT JOIN playlist_tracks pt ON pt.playlist_id = p.id "
            "GROUP BY p.id ORDER BY p.created_at, p.id"))) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.push_back({
            query.value(0).toLongLong(),
            query.value(1).toString(),
            QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs),
            query.value(3).toInt()
        });
    }
    return result;
}

std::vector<TrackInfo> LibraryRepository::playlistTracks(qint64 playlistId) const
{
    std::vector<TrackInfo> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.* FROM tracks t "
        "JOIN playlist_tracks pt ON pt.track_id = t.id "
        "WHERE pt.playlist_id = ? ORDER BY pt.position"));
    query.addBindValue(playlistId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.push_back(readTrackRow(query));
    }
    return result;
}

std::vector<TrackInfo> LibraryRepository::queryPlaylistTracks(
    qint64 playlistId, const QString& searchText, int limit, int offset) const
{
    std::vector<TrackInfo> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.* FROM tracks t "
        "JOIN playlist_tracks pt ON pt.track_id = t.id "
        "WHERE pt.playlist_id = ? "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY pt.position LIMIT ? OFFSET ?"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(playlistId);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.push_back(readTrackRow(query));
    }
    return result;
}

std::vector<qint64> LibraryRepository::playlistTrackIds(
    qint64 playlistId, const QString& searchText, int limit, int offset) const
{
    std::vector<qint64> ids;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.id FROM tracks t "
        "JOIN playlist_tracks pt ON pt.track_id = t.id "
        "WHERE pt.playlist_id = ? "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY pt.position LIMIT ? OFFSET ?"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(playlistId);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return ids;
    }
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return ids;
}

qint64 LibraryRepository::playlistTrackCount(
    qint64 playlistId, const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM tracks t "
        "JOIN playlist_tracks pt ON pt.track_id = t.id "
        "WHERE pt.playlist_id = ? "
        "AND (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(playlistId);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

bool LibraryRepository::savePlaybackQueue(
    const std::vector<qint64>& trackIds, std::size_t position,
    PlaybackMode mode)
{
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }
    QSqlQuery clearQuery(m_database);
    if (!clearQuery.exec(QStringLiteral("DELETE FROM playback_queue"))) {
        m_lastError = clearQuery.lastError().text();
        m_database.rollback();
        return false;
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO playback_queue(position, track_id) VALUES (?, ?)"));
    for (std::size_t index = 0; index < trackIds.size(); ++index) {
        insertQuery.bindValue(0, static_cast<qlonglong>(index));
        insertQuery.bindValue(1, trackIds[index]);
        if (!insertQuery.exec()) {
            m_lastError = insertQuery.lastError().text();
            m_database.rollback();
            return false;
        }
    }

    const std::size_t boundedPosition = trackIds.empty()
        ? 0 : std::min(position, trackIds.size() - 1);
    const qint64 currentTrackId = trackIds.empty()
        ? 0 : trackIds[boundedPosition];
    QSqlQuery stateQuery(m_database);
    stateQuery.prepare(QStringLiteral(
        "INSERT INTO playback_state "
        "(singleton, current_track_id, position, mode, updated_at) "
        "VALUES (1, ?, ?, ?, ?) "
        "ON CONFLICT(singleton) DO UPDATE SET "
        "current_track_id=excluded.current_track_id, "
        "position=excluded.position, mode=excluded.mode, "
        "updated_at=excluded.updated_at"));
    stateQuery.addBindValue(currentTrackId);
    stateQuery.addBindValue(static_cast<qlonglong>(boundedPosition));
    stateQuery.addBindValue(static_cast<int>(mode));
    stateQuery.addBindValue(
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!stateQuery.exec() || !m_database.commit()) {
        m_lastError = stateQuery.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool LibraryRepository::savePlaybackCursor(std::size_t position,
                                           PlaybackMode mode)
{
    QSqlQuery queueQuery(m_database);
    queueQuery.prepare(QStringLiteral(
        "SELECT track_id FROM playback_queue WHERE position = ?"));
    queueQuery.addBindValue(static_cast<qlonglong>(position));
    if (!queueQuery.exec() || !queueQuery.next()) {
        m_lastError = queueQuery.lastError().text();
        return false;
    }

    QSqlQuery stateQuery(m_database);
    stateQuery.prepare(QStringLiteral(
        "INSERT INTO playback_state "
        "(singleton, current_track_id, position, mode, updated_at) "
        "VALUES (1, ?, ?, ?, ?) "
        "ON CONFLICT(singleton) DO UPDATE SET "
        "current_track_id=excluded.current_track_id, "
        "position=excluded.position, mode=excluded.mode, "
        "updated_at=excluded.updated_at"));
    stateQuery.addBindValue(queueQuery.value(0).toLongLong());
    stateQuery.addBindValue(static_cast<qlonglong>(position));
    stateQuery.addBindValue(static_cast<int>(mode));
    stateQuery.addBindValue(
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!stateQuery.exec()) {
        m_lastError = stateQuery.lastError().text();
        return false;
    }
    return true;
}

std::optional<PersistedPlaybackQueue>
LibraryRepository::loadPlaybackQueue() const
{
    QSqlQuery stateQuery(m_database);
    if (!stateQuery.exec(QStringLiteral(
            "SELECT current_track_id, position, mode FROM playback_state "
            "WHERE singleton = 1")) ||
        !stateQuery.next()) {
        if (stateQuery.lastError().isValid()) {
            m_lastError = stateQuery.lastError().text();
        }
        return std::nullopt;
    }

    const qint64 currentTrackId = stateQuery.value(0).toLongLong();
    const qint64 storedPosition = std::max<qint64>(0, stateQuery.value(1).toLongLong());
    const int storedMode = stateQuery.value(2).toInt();
    PersistedPlaybackQueue state;
    state.mode = storedMode >= static_cast<int>(PlaybackMode::Sequential) &&
                 storedMode <= static_cast<int>(PlaybackMode::RepeatAll)
        ? static_cast<PlaybackMode>(storedMode)
        : PlaybackMode::Sequential;

    QSqlQuery queueQuery(m_database);
    if (!queueQuery.exec(QStringLiteral(
            "SELECT track_id FROM playback_queue ORDER BY position"))) {
        m_lastError = queueQuery.lastError().text();
        return std::nullopt;
    }
    while (queueQuery.next()) {
        state.trackIds.push_back(queueQuery.value(0).toLongLong());
    }
    if (state.trackIds.empty()) {
        state.position = 0;
        return state;
    }

    const auto current = std::find(
        state.trackIds.begin(), state.trackIds.end(), currentTrackId);
    state.position = current != state.trackIds.end()
        ? static_cast<std::size_t>(
              std::distance(state.trackIds.begin(), current))
        : std::min<std::size_t>(
              static_cast<std::size_t>(storedPosition),
              state.trackIds.size() - 1);
    return state;
}

bool LibraryRepository::recordPlayback(qint64 trackId, int maxEntries)
{
    if (trackId <= 0) {
        m_lastError = QStringLiteral("Invalid playback history track ID");
        return false;
    }
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO playback_history(track_id, played_at) VALUES (?, ?)"));
    insert.addBindValue(trackId);
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(
        Qt::ISODateWithMs));
    if (!insert.exec()) {
        m_lastError = insert.lastError().text();
        m_database.rollback();
        return false;
    }

    QSqlQuery prune(m_database);
    prune.prepare(QStringLiteral(
        "DELETE FROM playback_history WHERE id NOT IN ("
        "SELECT id FROM playback_history ORDER BY id DESC LIMIT ?)"));
    prune.addBindValue(std::clamp(maxEntries, 5, 10000));
    if (!prune.exec() || !m_database.commit()) {
        m_lastError = prune.lastError().isValid()
            ? prune.lastError().text()
            : m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool LibraryRepository::trimPlaybackHistory(int maxEntries)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM playback_history WHERE id NOT IN ("
        "SELECT id FROM playback_history ORDER BY id DESC LIMIT ?)"));
    query.addBindValue(std::clamp(maxEntries, 5, 10000));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

std::vector<TrackInfo> LibraryRepository::queryPlaybackHistory(
    const QString& searchText, int limit, int offset) const
{
    std::vector<TrackInfo> tracks;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.* FROM playback_history AS h "
        "JOIN tracks AS t ON t.id = h.track_id "
        "WHERE (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE) "
        "ORDER BY h.id DESC LIMIT ? OFFSET ?"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 10000));
    query.addBindValue(std::max(0, offset));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return tracks;
    }
    while (query.next()) {
        tracks.push_back(readTrackRow(query));
    }
    return tracks;
}

qint64 LibraryRepository::playbackHistoryCount(
    const QString& searchText) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM playback_history AS h "
        "JOIN tracks AS t ON t.id = h.track_id "
        "WHERE (? = '' OR t.title LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.artist LIKE ? ESCAPE '\\' COLLATE NOCASE "
        "OR t.album LIKE ? ESCAPE '\\' COLLATE NOCASE)"));
    const QString pattern = escapedSearchPattern(searchText);
    query.addBindValue(searchText);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.value(0).toLongLong();
}

} // namespace isac::library
