#include "repository.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString toDatabaseTime(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromDatabaseTime(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}

QString queryError(const QSqlQuery &query)
{
    return query.lastError().text();
}
}

Repository::Repository(QString databasePath)
    : m_databasePath(std::move(databasePath))
    , m_connectionName(QStringLiteral("yt-client-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

Repository::~Repository()
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = {};
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Repository::open(QString *error)
{
    if (m_database.isOpen())
        return true;

    if (m_databasePath.isEmpty()) {
        const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!QDir().mkpath(dataDirectory)) {
            setError(error, QStringLiteral("Could not create application data directory."));
            return false;
        }
        m_databasePath = QDir(dataDirectory).filePath(QStringLiteral("yt-client.sqlite3"));
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, queryError(query));
        return false;
    }
    if (m_databasePath != QStringLiteral(":memory:")
        && !query.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
        setError(error, queryError(query));
        return false;
    }
    query.finish();

    return migrate(error);
}

QList<Category> Repository::categories(QString *error) const
{
    QList<Category> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id, name FROM categories ORDER BY sort_order, name"))) {
        setError(error, queryError(query));
        return result;
    }

    while (query.next())
        result.append({query.value(0).toLongLong(), query.value(1).toString()});
    return result;
}

qint64 Repository::addCategory(const QString &name, QString *error)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setError(error, QStringLiteral("Category name cannot be empty."));
        return -1;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO categories(name, sort_order) "
        "VALUES(?, COALESCE((SELECT MAX(sort_order) + 1 FROM categories), 0))"));
    query.addBindValue(trimmedName);
    if (!query.exec()) {
        setError(error, queryError(query));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool Repository::renameCategory(qint64 id, const QString &name, QString *error)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setError(error, QStringLiteral("Category name cannot be empty."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE categories SET name = ? WHERE id = ?"));
    query.addBindValue(trimmedName);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error, QStringLiteral("Category does not exist."));
        return false;
    }
    return true;
}

bool Repository::removeCategory(qint64 id, QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM categories WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::moveCategory(qint64 id, int targetIndex, QString *error)
{
    QList<qint64> ids;
    QSqlQuery selectQuery(m_database);
    if (!selectQuery.exec(QStringLiteral("SELECT id FROM categories ORDER BY sort_order, name"))) {
        setError(error, queryError(selectQuery));
        return false;
    }
    while (selectQuery.next())
        ids.append(selectQuery.value(0).toLongLong());
    if (selectQuery.lastError().isValid()) {
        setError(error, queryError(selectQuery));
        return false;
    }
    selectQuery.finish();

    const int sourceIndex = ids.indexOf(id);
    if (sourceIndex < 0) {
        setError(error, QStringLiteral("Category does not exist."));
        return false;
    }
    if (targetIndex < 0 || targetIndex >= ids.size()) {
        setError(error, QStringLiteral("Target index is out of range."));
        return false;
    }
    if (sourceIndex == targetIndex)
        return true;

    ids.move(sourceIndex, targetIndex);
    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    QSqlQuery updateQuery(m_database);
    updateQuery.prepare(QStringLiteral("UPDATE categories SET sort_order = ? WHERE id = ?"));
    for (int index = 0; index < ids.size(); ++index) {
        updateQuery.bindValue(0, index);
        updateQuery.bindValue(1, ids.at(index));
        if (!updateQuery.exec()) {
            m_database.rollback();
            setError(error, queryError(updateQuery));
            return false;
        }
        if (updateQuery.numRowsAffected() != 1) {
            m_database.rollback();
            setError(error, QStringLiteral("Category does not exist."));
            return false;
        }
    }
    if (!m_database.commit()) {
        const QString commitError = m_database.lastError().text();
        m_database.rollback();
        setError(error, commitError);
        return false;
    }
    return true;
}

QList<Channel> Repository::channels(std::optional<qint64> categoryId, QString *error) const
{
    QList<Channel> result;
    QSqlQuery query(m_database);
    if (categoryId) {
        query.prepare(QStringLiteral(
            "SELECT c.id, c.original_input, c.handle, c.title, c.avatar_url, "
            "c.uploads_playlist_id, c.metadata_fetched_at "
            "FROM channels c "
            "JOIN category_channels cc ON cc.channel_id = c.id "
            "WHERE cc.category_id = ? ORDER BY c.title COLLATE NOCASE"));
        query.addBindValue(*categoryId);
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, original_input, handle, title, avatar_url, uploads_playlist_id, "
            "metadata_fetched_at FROM channels ORDER BY title COLLATE NOCASE"));
    }

    if (!query.exec()) {
        setError(error, queryError(query));
        return result;
    }

    while (query.next()) {
        result.append({
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            query.value(4).toString(),
            query.value(5).toString(),
            fromDatabaseTime(query.value(6).toString()),
        });
    }
    return result;
}

bool Repository::upsertChannel(const Channel &channel, QString *error)
{
    if (channel.id.isEmpty() || channel.title.isEmpty() || channel.uploadsPlaylistId.isEmpty()) {
        setError(error, QStringLiteral("Channel metadata is incomplete."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO channels(id, original_input, handle, title, avatar_url, "
        "uploads_playlist_id, metadata_fetched_at) VALUES(?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET original_input = excluded.original_input, "
        "handle = excluded.handle, title = excluded.title, avatar_url = excluded.avatar_url, "
        "uploads_playlist_id = excluded.uploads_playlist_id, "
        "metadata_fetched_at = excluded.metadata_fetched_at"));
    query.addBindValue(channel.id);
    query.addBindValue(channel.originalInput);
    query.addBindValue(channel.handle);
    query.addBindValue(channel.title);
    query.addBindValue(channel.avatarUrl);
    query.addBindValue(channel.uploadsPlaylistId);
    query.addBindValue(toDatabaseTime(channel.metadataFetchedAt));
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::removeChannel(const QString &channelId, QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM channels WHERE id = ?"));
    query.addBindValue(channelId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::setChannelCategories(
    const QString &channelId,
    const QList<qint64> &categoryIds,
    QString *error)
{
    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    QSqlQuery removeQuery(m_database);
    removeQuery.prepare(QStringLiteral("DELETE FROM category_channels WHERE channel_id = ?"));
    removeQuery.addBindValue(channelId);
    if (!removeQuery.exec()) {
        m_database.rollback();
        setError(error, queryError(removeQuery));
        return false;
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO category_channels(category_id, channel_id) VALUES(?, ?)"));
    for (const qint64 categoryId : categoryIds) {
        insertQuery.bindValue(0, categoryId);
        insertQuery.bindValue(1, channelId);
        if (!insertQuery.exec()) {
            m_database.rollback();
            setError(error, queryError(insertQuery));
            return false;
        }
    }

    if (!m_database.commit()) {
        setError(error, m_database.lastError().text());
        return false;
    }
    return true;
}

bool Repository::setChannelCategoryMembership(
    const QString &channelId,
    qint64 categoryId,
    bool member,
    QString *error)
{
    QSqlQuery query(m_database);
    if (member) {
        query.prepare(QStringLiteral(
            "INSERT INTO category_channels(category_id, channel_id) VALUES(?, ?) "
            "ON CONFLICT(category_id, channel_id) DO NOTHING"));
        query.addBindValue(categoryId);
        query.addBindValue(channelId);
    } else {
        query.prepare(QStringLiteral(
            "DELETE FROM category_channels WHERE category_id = ? AND channel_id = ?"));
        query.addBindValue(categoryId);
        query.addBindValue(channelId);
    }
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

QList<qint64> Repository::categoryIdsForChannel(const QString &channelId, QString *error) const
{
    QList<qint64> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT category_id FROM category_channels WHERE channel_id = ? ORDER BY category_id"));
    query.addBindValue(channelId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return result;
    }
    while (query.next())
        result.append(query.value(0).toLongLong());
    return result;
}

bool Repository::upsertVideos(const QList<Video> &videos, QString *error)
{
    if (videos.isEmpty())
        return true;
    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO videos(id, channel_id, title, published_at, is_broadcast, "
        "broadcast_state, fetched_at, duration_seconds) VALUES(?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET channel_id = excluded.channel_id, "
        "title = excluded.title, published_at = excluded.published_at, "
        "is_broadcast = excluded.is_broadcast, broadcast_state = excluded.broadcast_state, "
        "fetched_at = excluded.fetched_at, duration_seconds = excluded.duration_seconds"));
    for (const Video &video : videos) {
        query.bindValue(0, video.id);
        query.bindValue(1, video.channelId);
        query.bindValue(2, video.title);
        query.bindValue(3, toDatabaseTime(video.publishedAt));
        query.bindValue(4, video.isBroadcast);
        query.bindValue(5, video.broadcastState);
        query.bindValue(6, toDatabaseTime(video.fetchedAt));
        query.bindValue(7, video.durationSeconds);
        if (!query.exec()) {
            m_database.rollback();
            setError(error, queryError(query));
            return false;
        }
    }

    if (!m_database.commit()) {
        setError(error, m_database.lastError().text());
        return false;
    }
    return true;
}

QList<Video> Repository::feed(
    std::optional<qint64> categoryId,
    int shortVideoCutoffSeconds,
    int limit,
    QString *error) const
{
    return feedPage(categoryId, shortVideoCutoffSeconds, {}, {}, limit, error);
}

QList<Video> Repository::feedPage(
    std::optional<qint64> categoryId,
    int shortVideoCutoffSeconds,
    const QDateTime &publishedBefore,
    const QString &idBefore,
    int limit,
    QString *error) const
{
    QList<Video> result;
    QSqlQuery query(m_database);
    QString statement = QStringLiteral(
        "SELECT v.id, v.channel_id, c.title, v.title, v.published_at, v.is_broadcast, "
        "v.broadcast_state, v.fetched_at, v.duration_seconds, "
        "CASE WHEN w.video_id IS NULL OR v.duration_seconds <= 0 THEN -1 "
        "ELSE MIN(100, MAX(0, w.last_position_seconds) * 100 / v.duration_seconds) END "
        "FROM videos v "
        "JOIN channels c ON c.id = v.channel_id "
        "LEFT JOIN video_watch_time w ON w.video_id = v.id ");
    if (categoryId) {
        statement += QStringLiteral(
            "JOIN category_channels cc ON cc.channel_id = c.id "
            "WHERE v.is_broadcast = 0 AND v.duration_seconds > ? AND cc.category_id = ? ");
    } else {
        statement += QStringLiteral("WHERE v.is_broadcast = 0 AND v.duration_seconds > ? ");
    }
    const bool useCursor = publishedBefore.isValid() && !idBefore.isEmpty();
    if (useCursor) {
        statement += QStringLiteral(
            "AND (v.published_at < ? OR (v.published_at = ? AND v.id < ?)) ");
    }
    statement += QStringLiteral("ORDER BY v.published_at DESC, v.id DESC LIMIT ?");
    query.prepare(statement);
    query.addBindValue(qMax(0, shortVideoCutoffSeconds));
    if (categoryId)
        query.addBindValue(*categoryId);
    if (useCursor) {
        const QString cursorTime = toDatabaseTime(publishedBefore);
        query.addBindValue(cursorTime);
        query.addBindValue(cursorTime);
        query.addBindValue(idBefore);
    }
    query.addBindValue(qMax(1, limit));

    if (!query.exec()) {
        setError(error, queryError(query));
        return result;
    }

    while (query.next()) {
        result.append({
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            fromDatabaseTime(query.value(4).toString()),
            query.value(5).toBool(),
            query.value(6).toString(),
            fromDatabaseTime(query.value(7).toString()),
            query.value(8).toInt(),
            query.value(9).toInt(),
        });
    }
    return result;
}

QList<ChannelHistoryState> Repository::channelHistoryStates(QString *error) const
{
    QList<ChannelHistoryState> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT channel_id, next_page_token, history_complete FROM channel_history "
        "ORDER BY channel_id"));
    if (!query.exec()) {
        setError(error, queryError(query));
        return result;
    }
    while (query.next()) {
        result.append({
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toBool(),
        });
    }
    return result;
}

bool Repository::initializeChannelHistory(
    const QString &channelId,
    const QString &nextPageToken,
    QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO channel_history(channel_id, next_page_token, history_complete) "
        "VALUES(?, ?, ?) ON CONFLICT(channel_id) DO NOTHING"));
    query.addBindValue(channelId);
    // A null QString binds as SQL NULL; store an empty string instead.
    query.addBindValue(nextPageToken.isNull() ? QString(QStringLiteral("")) : nextPageToken);
    query.addBindValue(nextPageToken.isEmpty() ? 1 : 0);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::setChannelHistoryState(
    const QString &channelId,
    const QString &nextPageToken,
    bool historyComplete,
    QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO channel_history(channel_id, next_page_token, history_complete) "
        "VALUES(?, ?, ?) ON CONFLICT(channel_id) DO UPDATE SET "
        "next_page_token = excluded.next_page_token, "
        "history_complete = excluded.history_complete"));
    query.addBindValue(channelId);
    // A null QString binds as SQL NULL; store an empty string instead.
    query.addBindValue(nextPageToken.isNull() ? QString(QStringLiteral("")) : nextPageToken);
    query.addBindValue(historyComplete ? 1 : 0);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::historyIncomplete(std::optional<qint64> categoryId, QString *error) const
{
    QSqlQuery query(m_database);
    QString statement = QStringLiteral(
        "SELECT COUNT(*) FROM channels c ");
    if (categoryId)
        statement += QStringLiteral(
            "JOIN category_channels cc ON cc.channel_id = c.id "
            "WHERE cc.category_id = ? AND ");
    else {
        statement += QStringLiteral("WHERE ");
    }
    statement += QStringLiteral(
        "NOT EXISTS(SELECT 1 FROM channel_history h "
        "WHERE h.channel_id = c.id AND h.history_complete = 1)");
    query.prepare(statement);
    if (categoryId)
        query.addBindValue(*categoryId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("Could not read channel history state."));
        return false;
    }
    return query.value(0).toInt() > 0;
}

qint64 Repository::databaseSizeBytes() const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA page_count")) || !query.next())
        return 0;
    const qint64 pageCount = query.value(0).toLongLong();
    query.finish();
    if (!query.exec(QStringLiteral("PRAGMA page_size")) || !query.next())
        return 0;
    return pageCount * query.value(0).toLongLong();
}

bool Repository::canFetchMoreHistory() const
{
    return databaseSizeBytes() < historyFetchDatabaseBytes;
}

bool Repository::pruneVideoMetadataToLimit(qint64 maximumBytes, QString *error)
{
    if (databaseSizeBytes() <= maximumBytes)
        return true;

    int batchSize = 16;
    while (databaseSizeBytes() > maximumBytes) {
        QSqlQuery deleteQuery(m_database);
        deleteQuery.prepare(QStringLiteral(
            "DELETE FROM videos WHERE id IN ("
            "SELECT id FROM videos ORDER BY published_at ASC, id ASC LIMIT ?)"));
        deleteQuery.addBindValue(batchSize);
        if (!deleteQuery.exec()) {
            setError(error, queryError(deleteQuery));
            return false;
        }
        if (deleteQuery.numRowsAffected() == 0)
            break;
        QSqlQuery vacuumQuery(m_database);
        if (!vacuumQuery.exec(QStringLiteral("VACUUM"))) {
            setError(error, queryError(vacuumQuery));
            return false;
        }
        batchSize *= 2;
    }
    return true;
}

std::optional<Video> Repository::video(const QString &videoId, QString *error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT v.id, v.channel_id, c.title, v.title, v.published_at, v.is_broadcast, "
        "v.broadcast_state, v.fetched_at, v.duration_seconds FROM videos v "
        "JOIN channels c ON c.id = v.channel_id WHERE v.id = ?"));
    query.addBindValue(videoId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return Video{
        query.value(0).toString(),
        query.value(1).toString(),
        query.value(2).toString(),
        query.value(3).toString(),
        fromDatabaseTime(query.value(4).toString()),
        query.value(5).toBool(),
        query.value(6).toString(),
        fromDatabaseTime(query.value(7).toString()),
        query.value(8).toInt(),
    };
}

std::optional<WatchStats> Repository::watchStats(const QString &videoId, QString *error) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT video_id, watched_seconds, last_position_seconds, last_watched_at, watch_count "
        "FROM video_watch_time WHERE video_id = ?"));
    query.addBindValue(videoId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return WatchStats{
        query.value(0).toString(),
        query.value(1).toLongLong(),
        query.value(2).toInt(),
        fromDatabaseTime(query.value(3).toString()),
        query.value(4).toInt(),
    };
}

bool Repository::applyWatchProgress(
    const QString &videoId,
    qint64 watchedSecondsDelta,
    int lastPositionSeconds,
    bool countSession,
    QString *error)
{
    if (videoId.isEmpty()) {
        setError(error, QStringLiteral("Video id is required to record watch progress."));
        return false;
    }

    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    const QString timestamp = toDatabaseTime(QDateTime::currentDateTimeUtc());

    QSqlQuery watchQuery(m_database);
    watchQuery.prepare(QStringLiteral(
        "INSERT INTO video_watch_time(video_id, watched_seconds, last_position_seconds, "
        "last_watched_at, watch_count) VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(video_id) DO UPDATE SET "
        "watched_seconds = watched_seconds + excluded.watched_seconds, "
        "last_position_seconds = excluded.last_position_seconds, "
        "last_watched_at = excluded.last_watched_at, "
        "watch_count = watch_count + excluded.watch_count"));
    watchQuery.addBindValue(videoId);
    watchQuery.addBindValue(qMax<qint64>(0, watchedSecondsDelta));
    watchQuery.addBindValue(qMax(0, lastPositionSeconds));
    watchQuery.addBindValue(timestamp);
    watchQuery.addBindValue(countSession ? 1 : 0);
    if (!watchQuery.exec()) {
        m_database.rollback();
        setError(error, queryError(watchQuery));
        return false;
    }

    if (countSession) {
        // Resolve the channel from video metadata. When it is absent the
        // watch-time write stands alone and no incomplete history row is
        // recorded.
        QSqlQuery videoQuery(m_database);
        videoQuery.prepare(QStringLiteral("SELECT channel_id FROM videos WHERE id = ?"));
        videoQuery.addBindValue(videoId);
        if (!videoQuery.exec()) {
            m_database.rollback();
            setError(error, queryError(videoQuery));
            return false;
        }
        if (videoQuery.next()) {
            const QString channelId = videoQuery.value(0).toString();
            if (!channelId.isEmpty()) {
                QSqlQuery historyQuery(m_database);
                historyQuery.prepare(QStringLiteral(
                    "INSERT INTO history(datetime, video_id, channel_id) VALUES(?, ?, ?)"));
                historyQuery.addBindValue(timestamp);
                historyQuery.addBindValue(videoId);
                historyQuery.addBindValue(channelId);
                if (!historyQuery.exec()) {
                    m_database.rollback();
                    setError(error, queryError(historyQuery));
                    return false;
                }
            }
        }
    }

    if (!m_database.commit()) {
        setError(error, m_database.lastError().text());
        return false;
    }
    return true;
}

QList<HistoryEntry> Repository::watchHistory(QString *error) const
{
    QList<HistoryEntry> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT h.video_id, h.channel_id, c.title, v.title, h.datetime, "
        "CASE WHEN w.video_id IS NULL OR v.duration_seconds <= 0 THEN -1 "
        "ELSE MIN(100, MAX(0, w.last_position_seconds) * 100 / v.duration_seconds) END "
        "FROM history h "
        "JOIN videos v ON v.id = h.video_id "
        "JOIN channels c ON c.id = h.channel_id "
        "LEFT JOIN video_watch_time w ON w.video_id = h.video_id "
        "ORDER BY h.datetime DESC, h.id DESC"));
    if (!query.exec()) {
        setError(error, queryError(query));
        return result;
    }
    while (query.next()) {
        result.append({
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            fromDatabaseTime(query.value(4).toString()),
            query.value(5).toInt(),
        });
    }
    return result;
}

bool Repository::deleteWatchHistory(const QString &videoId, QString *error)
{
    if (videoId.isEmpty()) {
        setError(error, QStringLiteral("Video id is required to delete watch history."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM history WHERE video_id = ?"));
    query.addBindValue(videoId);
    if (!query.exec()) {
        setError(error, queryError(query));
        return false;
    }
    return true;
}

bool Repository::migrate(QString *error)
{
    QSqlQuery versionQuery(m_database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        setError(error, queryError(versionQuery));
        return false;
    }
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    constexpr int currentVersion = 5;
    if (version == currentVersion)
        return true;
    if (version < 0 || version > currentVersion) {
        setError(error, QStringLiteral("Database schema is newer than this application supports."));
        return false;
    }

    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    const QString channelHistoryTable = QStringLiteral(
        "CREATE TABLE channel_history("
        "channel_id TEXT PRIMARY KEY REFERENCES channels(id) ON DELETE CASCADE, "
        "next_page_token TEXT NOT NULL DEFAULT '', "
        "history_complete INTEGER NOT NULL DEFAULT 0)");

    const QString historyTable = QStringLiteral(
        "CREATE TABLE history("
        "id INTEGER PRIMARY KEY, "
        "datetime TEXT NOT NULL, "
        "video_id TEXT NOT NULL, "
        "channel_id TEXT NOT NULL)");
    const QString historyDatetimeIndex = QStringLiteral(
        "CREATE INDEX history_datetime ON history(datetime DESC)");

    QStringList statements;
    if (version == 0) {
        statements = {
            QStringLiteral(
                "CREATE TABLE categories("
                "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, sort_order INTEGER NOT NULL DEFAULT 0)"),
            QStringLiteral(
                "CREATE TABLE channels("
                "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)"),
            QStringLiteral(
                "CREATE TABLE category_channels("
                "category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE, "
                "channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                "PRIMARY KEY(category_id, channel_id))"),
            QStringLiteral(
                "CREATE TABLE videos("
                "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL, "
                "duration_seconds INTEGER NOT NULL DEFAULT -1)"),
            QStringLiteral("CREATE INDEX videos_published_at ON videos(published_at DESC)"),
            QStringLiteral(
                "CREATE TABLE video_watch_time("
                "video_id TEXT PRIMARY KEY, watched_seconds INTEGER NOT NULL DEFAULT 0, "
                "last_position_seconds INTEGER NOT NULL DEFAULT 0, "
                "last_watched_at TEXT NOT NULL DEFAULT '', "
                "watch_count INTEGER NOT NULL DEFAULT 0)"),
            channelHistoryTable,
            historyTable,
            historyDatetimeIndex,
            QStringLiteral("PRAGMA user_version = 5"),
        };
    } else {
        if (version == 1) {
            statements.append(QStringLiteral(
                "ALTER TABLE videos ADD COLUMN duration_seconds INTEGER NOT NULL DEFAULT -1"));
        }
        if (version <= 2) {
            statements.append(QStringLiteral(
                "CREATE TABLE video_watch_time("
                "video_id TEXT PRIMARY KEY, watched_seconds INTEGER NOT NULL DEFAULT 0, "
                "last_position_seconds INTEGER NOT NULL DEFAULT 0, "
                "last_watched_at TEXT NOT NULL DEFAULT '', "
                "watch_count INTEGER NOT NULL DEFAULT 0)"));
        }
        if (version <= 3)
            statements.append(channelHistoryTable);
        statements.append(historyTable);
        statements.append(historyDatetimeIndex);
        statements.append(QStringLiteral("PRAGMA user_version = 5"));
    }

    QSqlQuery query(m_database);
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            m_database.rollback();
            setError(error, queryError(query));
            return false;
        }
    }
    query.finish();

    if (!m_database.commit()) {
        setError(error, m_database.lastError().text());
        return false;
    }
    return true;
}

void Repository::setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}
