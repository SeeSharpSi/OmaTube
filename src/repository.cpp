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
        "broadcast_state, fetched_at) VALUES(?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET channel_id = excluded.channel_id, "
        "title = excluded.title, published_at = excluded.published_at, "
        "is_broadcast = excluded.is_broadcast, broadcast_state = excluded.broadcast_state, "
        "fetched_at = excluded.fetched_at"));
    for (const Video &video : videos) {
        query.bindValue(0, video.id);
        query.bindValue(1, video.channelId);
        query.bindValue(2, video.title);
        query.bindValue(3, toDatabaseTime(video.publishedAt));
        query.bindValue(4, video.isBroadcast);
        query.bindValue(5, video.broadcastState);
        query.bindValue(6, toDatabaseTime(video.fetchedAt));
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
    int limit,
    QString *error) const
{
    QList<Video> result;
    QSqlQuery query(m_database);
    QString statement = QStringLiteral(
        "SELECT v.id, v.channel_id, c.title, v.title, v.published_at, v.is_broadcast, "
        "v.broadcast_state, v.fetched_at FROM videos v "
        "JOIN channels c ON c.id = v.channel_id ");
    if (categoryId) {
        statement += QStringLiteral(
            "JOIN category_channels cc ON cc.channel_id = c.id "
            "WHERE v.is_broadcast = 0 AND cc.category_id = ? ");
    } else {
        statement += QStringLiteral("WHERE v.is_broadcast = 0 ");
    }
    statement += QStringLiteral("ORDER BY v.published_at DESC LIMIT ?");
    query.prepare(statement);
    if (categoryId)
        query.addBindValue(*categoryId);
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
        });
    }
    return result;
}

bool Repository::pruneVideoMetadata(const QDateTime &olderThan, QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM videos WHERE fetched_at < ?"));
    query.addBindValue(toDatabaseTime(olderThan));
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
    if (version == 1)
        return true;
    if (version != 0) {
        setError(error, QStringLiteral("Database schema is newer than this application supports."));
        return false;
    }

    if (!m_database.transaction()) {
        setError(error, m_database.lastError().text());
        return false;
    }

    const QStringList statements = {
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
            "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX videos_published_at ON videos(published_at DESC)"),
        QStringLiteral("PRAGMA user_version = 1"),
    };

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
