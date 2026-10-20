#include "LibraryRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace isac::library {
namespace {

QString sortColumn(TrackSort sort)
{
    switch (sort) {
    case TrackSort::Title: return QStringLiteral("title");
    case TrackSort::Artist: return QStringLiteral("artist");
    case TrackSort::Album: return QStringLiteral("album");
    case TrackSort::AddedAt: return QStringLiteral("added_at");
    }
    return QStringLiteral("added_at");
}

QString sortExpression(TrackSort sort)
{
    const QString column = sortColumn(sort);
    return sort == TrackSort::AddedAt
        ? column
        : column + QStringLiteral(" COLLATE NOCASE");
}

QString escapedSearchPattern(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QStringLiteral("%") + escaped + QStringLiteral("%");
}

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
            "PRIMARY KEY (playlist_id, track_id))")
    };

    for (const QString& statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
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
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
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
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE playlists SET name = ? WHERE id = ?"));
    query.addBindValue(name.trimmed());
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

} // namespace isac::library
