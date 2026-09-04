#include "models/categorymodel.h"
#include "models/feedmodel.h"
#include "models/historymodel.h"
#include "models/watchnextmodel.h"
#include "repository.h"

#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class RepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void persistentDatabaseOpens();
    void migratesVersionOneDatabase();
    void migratesVersionTwoDatabase();
    void migratesVersionThreeDatabase();
    void migratesVersionFourDatabase();
    void migratesVersionFiveDatabase();
    void watchNextAddRemoveAndCap();
    void watchNextMoveReorders();
    void watchNextDropsWithChannelRemoval();
    void categoryLifecycle();
    void categoryMembershipFiltersChannels();
    void importChannelsUpsertsFullMetadataByStableId();
    void importChannelsCreatesAndReusesCategories();
    void importChannelsMergesMembershipsAndIsIdempotent();
    void importChannelsRollsBackWholeBatch();
    void importCategoriesPreservesOrderMembershipsAndIsIdempotent();
    void feedExcludesBroadcastsShortVideosAndFiltersCategories();
    void provisionalMetadataPreservesKnownDuration();
    void feedPagePaginatesWithKeysetCursor();
    void channelHistoryStateLifecycle();
    void appliesWatchProgressAndSurvivesPruning();
    void prunesOldestVideosWhenOverStorageLimit();
    void watchHistoryInsertsAndOrders();
    void watchHistoryIgnoresUncountedSessions();
    void watchHistorySkipsMissingVideoMetadata();
    void deleteWatchHistoryRemovesOnlyHistory();
    void modelsExposeExpectedRoles();
    void historyModelExposesExpectedRoles();
    void watchNextModelExposesExpectedRoles();
};

namespace {
Channel makeChannel(const QString &id, const QString &title)
{
    return {
        id,
        QStringLiteral("@input"),
        QStringLiteral("@handle"),
        title,
        {},
        QStringLiteral("UU%1").arg(id),
        QDateTime::currentDateTimeUtc(),
    };
}

Video makeVideo(
    const QString &id,
    const QString &channelId,
    const QDateTime &publishedAt,
    bool isBroadcast = false,
    int durationSeconds = 600)
{
    return {
        id,
        channelId,
        {},
        QStringLiteral("Video %1").arg(id),
        publishedAt,
        isBroadcast,
        isBroadcast ? QStringLiteral("live") : QStringLiteral("none"),
        QDateTime::currentDateTimeUtc(),
        durationSeconds,
    };
}
}

void RepositoryTest::persistentDatabaseOpens()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));

    {
        Repository repository(databasePath);
        QString error;
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY2(repository.addCategory(QStringLiteral("News"), &error) > 0, qPrintable(error));
    }

    Repository repository(databasePath);
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QCOMPARE(repository.categories(&error), QList<Category>({{1, QStringLiteral("News")}}));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void RepositoryTest::migratesVersionOneDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    const QString connectionName = QStringLiteral("version-one-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE channels("
                     "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                     "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)")),
                 qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE videos("
                     "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                     "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                     "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL)")),
                 qPrintable(query.lastError().text()));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO channels VALUES("
            "'UCAlpha', '@input', '@handle', 'Alpha', '', 'UUAlpha', "
            "'2026-08-25T12:00:00.000Z')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO videos VALUES("
            "'regular', 'UCAlpha', 'Regular', '2026-08-25T12:00:00.000Z', 0, 'none', "
            "'2026-08-25T12:00:00.000Z')")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Repository repository(databasePath);
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QCOMPARE(repository.channels().size(), 1);
    QCOMPARE(repository.feed().size(), 1);
    QCOMPARE(repository.feed().first().durationSeconds, -1);
    QVERIFY(repository.upsertVideos({makeVideo(
        QStringLiteral("regular"),
        QStringLiteral("UCAlpha"),
        QDateTime::currentDateTimeUtc())}, &error));
    QCOMPARE(repository.feed().size(), 1);

    QVERIFY2(repository.applyWatchProgress(
                 QStringLiteral("regular"), 30, 45, true, &error),
             qPrintable(error));
    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("regular"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 30);
}

void RepositoryTest::migratesVersionTwoDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    const QString connectionName = QStringLiteral("version-two-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE channels("
                     "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                     "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)")),
                 qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE videos("
                     "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                     "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                     "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL, "
                     "duration_seconds INTEGER NOT NULL DEFAULT -1)")),
                 qPrintable(query.lastError().text()));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO channels VALUES("
            "'UCAlpha', '@input', '@handle', 'Alpha', '', 'UUAlpha', "
            "'2026-08-25T12:00:00.000Z')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO videos VALUES("
            "'regular', 'UCAlpha', 'Regular', '2026-08-25T12:00:00.000Z', 0, 'none', "
            "'2026-08-25T12:00:00.000Z', 600)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 2")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Repository repository(databasePath);
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QCOMPARE(repository.feed().size(), 1);
    QVERIFY(repository.video(QStringLiteral("regular"), &error).has_value());

    QVERIFY2(repository.applyWatchProgress(
                 QStringLiteral("regular"), 15, 20, true, &error),
             qPrintable(error));
    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("regular"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 15);
}

void RepositoryTest::migratesVersionThreeDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    const QString connectionName = QStringLiteral("version-three-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE categories("
                      "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, sort_order INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE channels("
                      "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                      "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE category_channels("
                      "category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE, "
                      "channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "PRIMARY KEY(category_id, channel_id))")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE videos("
                      "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                      "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL, "
                      "duration_seconds INTEGER NOT NULL DEFAULT -1)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE video_watch_time("
                      "video_id TEXT PRIMARY KEY, watched_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_position_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_watched_at TEXT NOT NULL DEFAULT '', "
                      "watch_count INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO channels VALUES("
            "'UCAlpha', '@input', '@handle', 'Alpha', '', 'UUAlpha', "
            "'2026-08-25T12:00:00.000Z')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO videos VALUES("
            "'regular', 'UCAlpha', 'Regular', '2026-08-25T12:00:00.000Z', 0, 'none', "
            "'2026-08-25T12:00:00.000Z', 600)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 3")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Repository repository(databasePath);
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QCOMPARE(repository.feed().size(), 1);
    QCOMPARE(repository.channelHistoryStates(&error).size(), 0);
    QVERIFY(repository.historyIncomplete(std::nullopt, &error));
    QVERIFY(repository.setChannelHistoryState(
        QStringLiteral("UCAlpha"), QStringLiteral("token-two"), false, &error));
    QCOMPARE(
        repository.channelHistoryStates(&error),
        QList<ChannelHistoryState>({
            {QStringLiteral("UCAlpha"), QStringLiteral("token-two"), false},
        }));
}

void RepositoryTest::migratesVersionFourDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    const QString connectionName = QStringLiteral("version-four-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE categories("
                      "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, sort_order INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE channels("
                      "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                      "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE category_channels("
                      "category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE, "
                      "channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "PRIMARY KEY(category_id, channel_id))")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE videos("
                      "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                      "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL, "
                      "duration_seconds INTEGER NOT NULL DEFAULT -1)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE video_watch_time("
                      "video_id TEXT PRIMARY KEY, watched_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_position_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_watched_at TEXT NOT NULL DEFAULT '', "
                      "watch_count INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE channel_history("
                      "channel_id TEXT PRIMARY KEY REFERENCES channels(id) ON DELETE CASCADE, "
                      "next_page_token TEXT NOT NULL DEFAULT '', "
                      "history_complete INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 4")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    {
        Repository repository(databasePath);
        QString error;
        QVERIFY2(repository.open(&error), qPrintable(error));
    }

    const QString validationConnectionName = QStringLiteral("version-four-validation");
    {
        QSqlDatabase validationDatabase =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), validationConnectionName);
        validationDatabase.setDatabaseName(databasePath);
        QVERIFY(validationDatabase.open());
        QSqlQuery validation(validationDatabase);
        QVERIFY2(validation.exec(QStringLiteral("PRAGMA user_version")),
                 qPrintable(validation.lastError().text()));
        QVERIFY(validation.next());
        QCOMPARE(validation.value(0).toInt(), 6);

        QVERIFY2(validation.exec(QStringLiteral("PRAGMA table_info(history)")),
                 qPrintable(validation.lastError().text()));
        QStringList columns;
        bool idPrimaryKey = false;
        bool datetimeNotNull = false;
        bool videoIdNotNull = false;
        bool channelIdNotNull = false;
        while (validation.next()) {
            const QString name = validation.value(1).toString();
            columns.append(name);
            const bool notNull = validation.value(3).toBool();
            const bool primaryKey = validation.value(5).toBool();
            if (name == QLatin1String("id")) {
                idPrimaryKey = primaryKey;
            } else if (name == QLatin1String("datetime")) {
                datetimeNotNull = notNull;
            } else if (name == QLatin1String("video_id")) {
                videoIdNotNull = notNull;
            } else if (name == QLatin1String("channel_id")) {
                channelIdNotNull = notNull;
            }
        }
        QCOMPARE(columns, QStringList({QStringLiteral("id"), QStringLiteral("datetime"),
                                       QStringLiteral("video_id"), QStringLiteral("channel_id")}));
        QVERIFY(idPrimaryKey);
        QVERIFY(datetimeNotNull);
        QVERIFY(videoIdNotNull);
        QVERIFY(channelIdNotNull);

        QVERIFY2(validation.exec(QStringLiteral("PRAGMA index_list(history)")),
                 qPrintable(validation.lastError().text()));
        QStringList indexNames;
        while (validation.next())
            indexNames.append(validation.value(1).toString());
        QVERIFY(indexNames.contains(QStringLiteral("history_datetime")));

        validationDatabase.close();
    }
    QSqlDatabase::removeDatabase(validationConnectionName);
}

void RepositoryTest::migratesVersionFiveDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    const QString connectionName = QStringLiteral("version-five-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE categories("
                      "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, sort_order INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE channels("
                      "id TEXT PRIMARY KEY, original_input TEXT NOT NULL, handle TEXT, title TEXT NOT NULL, "
                      "avatar_url TEXT, uploads_playlist_id TEXT NOT NULL, metadata_fetched_at TEXT NOT NULL)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE category_channels("
                      "category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE, "
                      "channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "PRIMARY KEY(category_id, channel_id))")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE videos("
                      "id TEXT PRIMARY KEY, channel_id TEXT NOT NULL REFERENCES channels(id) ON DELETE CASCADE, "
                      "title TEXT NOT NULL, published_at TEXT NOT NULL, is_broadcast INTEGER NOT NULL, "
                      "broadcast_state TEXT NOT NULL, fetched_at TEXT NOT NULL, "
                      "duration_seconds INTEGER NOT NULL DEFAULT -1)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE video_watch_time("
                      "video_id TEXT PRIMARY KEY, watched_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_position_seconds INTEGER NOT NULL DEFAULT 0, "
                      "last_watched_at TEXT NOT NULL DEFAULT '', "
                      "watch_count INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE channel_history("
                      "channel_id TEXT PRIMARY KEY REFERENCES channels(id) ON DELETE CASCADE, "
                      "next_page_token TEXT NOT NULL DEFAULT '', "
                      "history_complete INTEGER NOT NULL DEFAULT 0)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE TABLE history("
                      "id INTEGER PRIMARY KEY, datetime TEXT NOT NULL, "
                      "video_id TEXT NOT NULL, channel_id TEXT NOT NULL)")),
                  qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                      "CREATE INDEX history_datetime ON history(datetime DESC)")),
                  qPrintable(query.lastError().text()));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO channels VALUES("
            "'UCAlpha', '@input', '@handle', 'Alpha', '', 'UUAlpha', "
            "'2026-08-25T12:00:00.000Z')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO videos VALUES("
            "'regular', 'UCAlpha', 'Regular', '2026-08-25T12:00:00.000Z', 0, 'none', "
            "'2026-08-25T12:00:00.000Z', 600)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 5")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Repository repository(databasePath);
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(repository.watchNext(&error).isEmpty());
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(repository.addToWatchNext(QStringLiteral("regular"), &error), qPrintable(error));
    QCOMPARE(repository.watchNext(&error).size(), 1);
    QCOMPARE(repository.watchNext(&error).first().videoId, QStringLiteral("regular"));
    QCOMPARE(repository.watchNext(&error).first().channelTitle, QStringLiteral("Alpha"));

    const QString validationConnectionName = QStringLiteral("version-five-validation");
    {
        QSqlDatabase validationDatabase =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), validationConnectionName);
        validationDatabase.setDatabaseName(databasePath);
        QVERIFY(validationDatabase.open());
        QSqlQuery validation(validationDatabase);
        QVERIFY2(validation.exec(QStringLiteral("PRAGMA user_version")),
                 qPrintable(validation.lastError().text()));
        QVERIFY(validation.next());
        QCOMPARE(validation.value(0).toInt(), 6);
        validationDatabase.close();
    }
    QSqlDatabase::removeDatabase(validationConnectionName);
}

void RepositoryTest::watchNextAddRemoveAndCap()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(alpha, &error));

    // Unknown videos cannot join the queue.
    QVERIFY(!repository.addToWatchNext(QStringLiteral("missing"), &error));
    QCOMPARE(error, QStringLiteral("Video is not in the local library."));
    error.clear();
    QVERIFY(!repository.addToWatchNext(QString(), &error));
    QVERIFY(!error.isEmpty());
    error.clear();

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<Video> videos;
    for (int index = 0; index < Repository::watchNextMaxItems + 1; ++index) {
        videos.append(makeVideo(
            QStringLiteral("vid-%1").arg(index, 3, 10, QLatin1Char('0')),
            alpha.id,
            now.addSecs(-index)));
    }
    QVERIFY(repository.upsertVideos(videos, &error));

    for (int index = 0; index < Repository::watchNextMaxItems; ++index) {
        const QString id = QStringLiteral("vid-%1").arg(index, 3, 10, QLatin1Char('0'));
        QVERIFY2(repository.addToWatchNext(id, &error), qPrintable(error));
    }
    // Adding twice is idempotent and does not consume a second slot.
    QVERIFY2(
        repository.addToWatchNext(QStringLiteral("vid-000"), &error), qPrintable(error));
    QCOMPARE(repository.watchNext(&error).size(), Repository::watchNextMaxItems);

    const QString overflowId = QStringLiteral("vid-%1").arg(
        Repository::watchNextMaxItems, 3, 10, QLatin1Char('0'));
    QVERIFY(!repository.addToWatchNext(overflowId, &error));
    QVERIFY(error.contains(QStringLiteral("Watch Next is full")));
    error.clear();

    QVERIFY(repository.isInWatchNext(QStringLiteral("vid-000"), &error));
    QVERIFY(!repository.isInWatchNext(overflowId, &error));

    // Progress joins the queue rows like the feed.
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-000"), 300, 300, false, &error));
    const QList<WatchNextEntry> entries = repository.watchNext(&error);
    QCOMPARE(entries.size(), Repository::watchNextMaxItems);
    QCOMPARE(entries.first().videoId, QStringLiteral("vid-000"));
    QCOMPARE(entries.first().watchProgressPercent, 50);
    QCOMPARE(entries.first().position, 0);

    QVERIFY(repository.removeFromWatchNext(QStringLiteral("vid-000"), &error));
    QCOMPARE(repository.watchNext(&error).size(), Repository::watchNextMaxItems - 1);
    QVERIFY(!repository.isInWatchNext(QStringLiteral("vid-000"), &error));
    // Positions stay dense after removal.
    const QList<WatchNextEntry> afterRemove = repository.watchNext(&error);
    for (int index = 0; index < afterRemove.size(); ++index)
        QCOMPARE(afterRemove.at(index).position, index);
    // A freed slot accepts the previously rejected video.
    QVERIFY2(repository.addToWatchNext(overflowId, &error), qPrintable(error));
    QCOMPARE(repository.watchNext(&error).size(), Repository::watchNextMaxItems);
}

void RepositoryTest::watchNextMoveReorders()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos({
        makeVideo(QStringLiteral("vid-a"), alpha.id, now),
        makeVideo(QStringLiteral("vid-b"), alpha.id, now.addSecs(-60)),
        makeVideo(QStringLiteral("vid-c"), alpha.id, now.addSecs(-120)),
    }, &error));
    QVERIFY(repository.addToWatchNext(QStringLiteral("vid-a"), &error));
    QVERIFY(repository.addToWatchNext(QStringLiteral("vid-b"), &error));
    QVERIFY(repository.addToWatchNext(QStringLiteral("vid-c"), &error));

    QVERIFY(repository.moveWatchNext(QStringLiteral("vid-c"), 0, &error));
    QList<WatchNextEntry> entries = repository.watchNext(&error);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).videoId, QStringLiteral("vid-c"));
    QCOMPARE(entries.at(1).videoId, QStringLiteral("vid-a"));
    QCOMPARE(entries.at(2).videoId, QStringLiteral("vid-b"));

    // Moving to the same index is a no-op success.
    QVERIFY(repository.moveWatchNext(QStringLiteral("vid-c"), 0, &error));
    // Unknown videos and out-of-range targets are rejected.
    QVERIFY(!repository.moveWatchNext(QStringLiteral("missing"), 0, &error));
    QCOMPARE(error, QStringLiteral("Video is not in Watch Next."));
    error.clear();
    QVERIFY(!repository.moveWatchNext(QStringLiteral("vid-a"), -1, &error));
    QCOMPARE(error, QStringLiteral("Target index is out of range."));
    error.clear();
    QVERIFY(!repository.moveWatchNext(QStringLiteral("vid-a"), 3, &error));
    QCOMPARE(error, QStringLiteral("Target index is out of range."));
    error.clear();
    QCOMPARE(repository.watchNext(&error).at(1).videoId, QStringLiteral("vid-a"));
}

void RepositoryTest::watchNextDropsWithChannelRemoval()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos({
        makeVideo(QStringLiteral("vid-a"), alpha.id, now),
        makeVideo(QStringLiteral("vid-b"), beta.id, now),
    }, &error));
    QVERIFY(repository.addToWatchNext(QStringLiteral("vid-a"), &error));
    QVERIFY(repository.addToWatchNext(QStringLiteral("vid-b"), &error));
    QCOMPARE(repository.watchNext(&error).size(), 2);

    QVERIFY(repository.removeChannel(alpha.id, &error));
    const QList<WatchNextEntry> entries = repository.watchNext(&error);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().videoId, QStringLiteral("vid-b"));
}

void RepositoryTest::categoryLifecycle()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));

    const qint64 newsId = repository.addCategory(QStringLiteral(" News "), &error);
    QVERIFY2(newsId > 0, qPrintable(error));
    const qint64 musicId = repository.addCategory(QStringLiteral("Music"), &error);
    QVERIFY2(musicId > 0, qPrintable(error));
    const qint64 sportsId = repository.addCategory(QStringLiteral("Sports"), &error);
    QVERIFY2(sportsId > 0, qPrintable(error));
    const qint64 filmsId = repository.addCategory(QStringLiteral("Films"), &error);
    QVERIFY2(filmsId > 0, qPrintable(error));

    QCOMPARE(repository.categories(), QList<Category>({{newsId, QStringLiteral("News")},
                                                        {musicId, QStringLiteral("Music")},
                                                        {sportsId, QStringLiteral("Sports")},
                                                        {filmsId, QStringLiteral("Films")}}));
    QVERIFY(repository.moveCategory(sportsId, 0, &error));
    QCOMPARE(repository.categories().value(0).id, sportsId);
    QVERIFY(repository.moveCategory(sportsId, 3, &error));
    QCOMPARE(repository.categories().value(3).id, sportsId);
    const QList<Category> beforeNoOp = repository.categories();
    QVERIFY(repository.moveCategory(sportsId, 3, &error));
    QCOMPARE(repository.categories(), beforeNoOp);
    QVERIFY(!repository.moveCategory(-1, 0, &error));
    QCOMPARE(repository.categories(), beforeNoOp);
    QVERIFY(!repository.moveCategory(filmsId, -1, &error));
    QCOMPARE(repository.categories(), beforeNoOp);
    QVERIFY(!repository.moveCategory(filmsId, 4, &error));
    QCOMPARE(repository.categories(), beforeNoOp);
    QVERIFY(repository.renameCategory(newsId, QStringLiteral("Current events"), &error));
    QCOMPARE(repository.categories().value(0).name, QStringLiteral("Current events"));
    QVERIFY(repository.removeCategory(musicId, &error));
    QCOMPARE(repository.categories().size(), 3);
    QCOMPARE(repository.addCategory(QStringLiteral("  "), &error), -1);
    QCOMPARE(error, QStringLiteral("Category name cannot be empty."));
}

void RepositoryTest::categoryMembershipFiltersChannels()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 categoryId = repository.addCategory(QStringLiteral("Tech"), &error);
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    QVERIFY(repository.setChannelCategories(alpha.id, {categoryId}, &error));

    QCOMPARE(repository.channels().size(), 2);
    QCOMPARE(repository.channels(categoryId), QList<Channel>({alpha}));
    QCOMPARE(repository.categoryIdsForChannel(alpha.id), QList<qint64>({categoryId}));

    QVERIFY(repository.removeCategory(categoryId, &error));
    QCOMPARE(repository.categoryIdsForChannel(alpha.id).size(), 0);
    QCOMPARE(repository.channels().size(), 2);
}

void RepositoryTest::importChannelsUpsertsFullMetadataByStableId()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));

    const QDateTime firstFetched = QDateTime::fromString(
        QStringLiteral("2026-08-25T12:00:00.000Z"), Qt::ISODateWithMs);
    const QDateTime secondFetched = QDateTime::fromString(
        QStringLiteral("2026-08-26T12:00:00.000Z"), Qt::ISODateWithMs);
    Channel first{
        QStringLiteral("UCStable"), QStringLiteral("old-input"), QStringLiteral("@old"),
        QStringLiteral("Old title"), QStringLiteral("old-avatar"), QStringLiteral("UUold"),
        firstFetched};
    Channel updated{
        QStringLiteral("UCStable"), QStringLiteral("new-input"), QStringLiteral("@new"),
        QStringLiteral("New title"), QStringLiteral("new-avatar"), QStringLiteral("UUnew"),
        secondFetched};

    QVERIFY2(repository.importChannels({{first, {}}}, &error), qPrintable(error));
    QVERIFY2(repository.importChannels({{updated, {}}}, &error), qPrintable(error));
    QCOMPARE(repository.channels(), QList<Channel>({updated}));
}

void RepositoryTest::importChannelsCreatesAndReusesCategories()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 existingId = repository.addCategory(QStringLiteral("Existing"), &error);
    QVERIFY2(existingId > 0, qPrintable(error));
    const Channel channel = makeChannel(QStringLiteral("UCImport"), QStringLiteral("Imported"));

    QVERIFY2(repository.importChannels(
                  {{channel, {QStringLiteral("Existing"), QStringLiteral("Created")}}}, &error),
              qPrintable(error));
    QCOMPARE(repository.categories(), QList<Category>({
        {existingId, QStringLiteral("Existing")},
        {existingId + 1, QStringLiteral("Created")},
    }));
    QCOMPARE(repository.categoryIdsForChannel(channel.id, &error), QList<qint64>({existingId, existingId + 1}));
}

void RepositoryTest::importChannelsMergesMembershipsAndIsIdempotent()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 unrelatedId = repository.addCategory(QStringLiteral("Unrelated"), &error);
    QVERIFY2(unrelatedId > 0, qPrintable(error));
    const Channel channel = makeChannel(QStringLiteral("UCMerge"), QStringLiteral("Merge"));
    QVERIFY2(repository.upsertChannel(channel, &error), qPrintable(error));
    QVERIFY2(repository.setChannelCategories(channel.id, {unrelatedId}, &error), qPrintable(error));

    const Repository::ChannelImportRecord record{
        channel, {QStringLiteral("Imported"), QStringLiteral("Unrelated")}};
    QVERIFY2(repository.importChannels({record}, &error), qPrintable(error));
    const QList<Category> categoriesBefore = repository.categories(&error);
    const QList<qint64> membershipsBefore = repository.categoryIdsForChannel(channel.id, &error);
    QVERIFY2(repository.importChannels({record}, &error), qPrintable(error));

    QCOMPARE(repository.categories(), categoriesBefore);
    QCOMPARE(repository.categoryIdsForChannel(channel.id, &error), membershipsBefore);
    QCOMPARE(membershipsBefore.size(), 2);
}

void RepositoryTest::importChannelsRollsBackWholeBatch()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel valid = makeChannel(QStringLiteral("UCValid"), QStringLiteral("Valid"));
    const Channel invalid{
        QStringLiteral("UCInvalid"), QStringLiteral("input"), QStringLiteral("@invalid"), {},
        QStringLiteral("avatar"), QStringLiteral("UUinvalid"), QDateTime::currentDateTimeUtc()};

    QVERIFY2(!repository.importChannels({
                  {valid, {QStringLiteral("Rolled back")}},
                  {invalid, {QStringLiteral("Also rolled back")}},
              }, &error),
              "invalid later record must fail");
    QVERIFY2(!error.isEmpty(), "failed import must report an error");
    QCOMPARE(repository.channels().size(), 0);
    QCOMPARE(repository.categories().size(), 0);
    QCOMPARE(repository.categoryIdsForChannel(valid.id, &error).size(), 0);
}

void RepositoryTest::importCategoriesPreservesOrderMembershipsAndIsIdempotent()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 existingId = repository.addCategory(QStringLiteral("Existing"), &error);
    const qint64 tailId = repository.addCategory(QStringLiteral("Tail"), &error);
    QVERIFY2(existingId > 0 && tailId > 0, qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY2(repository.upsertChannel(alpha, &error), qPrintable(error));
    QVERIFY2(repository.upsertChannel(beta, &error), qPrintable(error));
    QVERIFY2(repository.setChannelCategories(alpha.id, {existingId}, &error), qPrintable(error));

    const QList<Repository::CategoryImportRecord> records{
        {QStringLiteral("Existing"), {beta.id, QStringLiteral("UCUnknown"), alpha.id}},
        {QStringLiteral("New one"), {beta.id, QStringLiteral("UCUnknown")}},
        {QStringLiteral("New two"), {QStringLiteral("UCUnknown")}},
    };
    QVERIFY2(repository.importCategories(records, &error), qPrintable(error));
    QCOMPARE(repository.categories(), QList<Category>({
        {existingId, QStringLiteral("Existing")},
        {tailId, QStringLiteral("Tail")},
        {tailId + 1, QStringLiteral("New one")},
        {tailId + 2, QStringLiteral("New two")},
    }));
    QCOMPARE(repository.categoryIdsForChannel(alpha.id, &error), QList<qint64>({existingId}));
    QCOMPARE(repository.categoryIdsForChannel(beta.id, &error), QList<qint64>({existingId, tailId + 1}));

    const QList<Category> categoriesBefore = repository.categories(&error);
    const QList<qint64> alphaMembershipsBefore = repository.categoryIdsForChannel(alpha.id, &error);
    const QList<qint64> betaMembershipsBefore = repository.categoryIdsForChannel(beta.id, &error);
    QVERIFY2(repository.importCategories(records, &error), qPrintable(error));
    QCOMPARE(repository.categories(), categoriesBefore);
    QCOMPARE(repository.categoryIdsForChannel(alpha.id, &error), alphaMembershipsBefore);
    QCOMPARE(repository.categoryIdsForChannel(beta.id, &error), betaMembershipsBefore);
}

void RepositoryTest::feedExcludesBroadcastsShortVideosAndFiltersCategories()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 categoryId = repository.addCategory(QStringLiteral("Selected"), &error);
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    QVERIFY(repository.setChannelCategories(alpha.id, {categoryId}, &error));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos({
        makeVideo(QStringLiteral("old"), alpha.id, now.addSecs(-60), false, 181),
        makeVideo(QStringLiteral("new"), beta.id, now),
        makeVideo(QStringLiteral("live"), alpha.id, now.addSecs(60), true),
        makeVideo(QStringLiteral("short"), alpha.id, now.addSecs(120), false, 180),
    }, &error));

    const QList<Video> allFeed = repository.feed();
    QCOMPARE(allFeed.size(), 2);
    QCOMPARE(allFeed.first().id, QStringLiteral("new"));
    QCOMPARE(allFeed.last().channelTitle, QStringLiteral("Alpha"));
    QCOMPARE(allFeed.first().watchProgressPercent, -1);

    const QList<Video> selectedFeed = repository.feed(categoryId);
    QCOMPARE(selectedFeed.size(), 1);
    QCOMPARE(selectedFeed.first().id, QStringLiteral("old"));

    const QList<Video> unfilteredFeed = repository.feed(std::nullopt, 0);
    QCOMPARE(unfilteredFeed.size(), 3);
    QCOMPARE(unfilteredFeed.first().id, QStringLiteral("short"));
}

void RepositoryTest::provisionalMetadataPreservesKnownDuration()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel channel = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(channel, &error));
    const QDateTime publishedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos(
        {makeVideo(QStringLiteral("video"), channel.id, publishedAt, false, 600)},
        &error));
    QVERIFY(repository.upsertVideos(
        {makeVideo(QStringLiteral("video"), channel.id, publishedAt, false, -1)},
        &error));

    const std::optional<Video> stored = repository.video(QStringLiteral("video"), &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    QCOMPARE(stored->durationSeconds, 600);
}

void RepositoryTest::feedPagePaginatesWithKeysetCursor()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 categoryId = repository.addCategory(QStringLiteral("Selected"), &error);
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    QVERIFY(repository.setChannelCategories(alpha.id, {categoryId}, &error));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime tie = now.addSecs(-100);
    QVERIFY(repository.upsertVideos({
        // Three videos sharing one timestamp exercise the id tiebreak.
        makeVideo(QStringLiteral("tie-a"), alpha.id, tie),
        makeVideo(QStringLiteral("tie-b"), alpha.id, tie),
        makeVideo(QStringLiteral("tie-c"), alpha.id, tie),
        makeVideo(QStringLiteral("newest"), beta.id, now),
        makeVideo(QStringLiteral("oldest"), beta.id, now.addSecs(-200)),
    }, &error));

    const QList<Video> firstPage = repository.feedPage(std::nullopt, 180, {}, {}, 2, &error);
    QCOMPARE(firstPage.size(), 2);
    QCOMPARE(firstPage.at(0).id, QStringLiteral("newest"));
    QCOMPARE(firstPage.at(1).id, QStringLiteral("tie-c"));

    const QList<Video> secondPage = repository.feedPage(
        std::nullopt,
        180,
        firstPage.last().publishedAt,
        firstPage.last().id,
        2,
        &error);
    QCOMPARE(secondPage.size(), 2);
    QCOMPARE(secondPage.at(0).id, QStringLiteral("tie-b"));
    QCOMPARE(secondPage.at(1).id, QStringLiteral("tie-a"));

    const QList<Video> thirdPage = repository.feedPage(
        std::nullopt,
        180,
        secondPage.last().publishedAt,
        secondPage.last().id,
        2,
        &error);
    QCOMPARE(thirdPage.size(), 1);
    QCOMPARE(thirdPage.at(0).id, QStringLiteral("oldest"));

    const QList<Video> exhausted = repository.feedPage(
        std::nullopt,
        180,
        thirdPage.last().publishedAt,
        thirdPage.last().id,
        2,
        &error);
    QVERIFY(exhausted.isEmpty());

    // Category scoping applies to every page.
    const QList<Video> categoryPage = repository.feedPage(categoryId, 180, {}, {}, 10, &error);
    QCOMPARE(categoryPage.size(), 3);
    QVERIFY(std::all_of(categoryPage.cbegin(), categoryPage.cend(), [&alpha](const Video &video) {
        return video.channelId == alpha.id;
    }));
}

void RepositoryTest::channelHistoryStateLifecycle()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    const qint64 categoryId = repository.addCategory(QStringLiteral("Tech"), &error);
    QVERIFY(repository.setChannelCategories(beta.id, {categoryId}, &error));

    // No state recorded yet: both scopes still have history to fetch.
    QVERIFY(repository.historyIncomplete(std::nullopt, &error));
    QVERIFY(repository.historyIncomplete(categoryId, &error));

    // initializeChannelHistory records a resume point but never rewinds one.
    QVERIFY(repository.initializeChannelHistory(alpha.id, QStringLiteral("token-two"), &error));
    QVERIFY(repository.initializeChannelHistory(alpha.id, QStringLiteral("token-one"), &error));
    QCOMPARE(
        repository.channelHistoryStates(&error),
        QList<ChannelHistoryState>({
            {QStringLiteral("UCAlpha"), QStringLiteral("token-two"), false},
        }));

    QVERIFY(repository.setChannelHistoryState(alpha.id, {}, true, &error));
    // Beta has no recorded state yet, so every scope still has history.
    QVERIFY(repository.historyIncomplete(std::nullopt, &error));
    QVERIFY(repository.historyIncomplete(categoryId, &error));

    QVERIFY(repository.setChannelHistoryState(beta.id, QStringLiteral("token-nine"), false, &error));
    QVERIFY(repository.historyIncomplete(categoryId, &error));
    QVERIFY(repository.setChannelHistoryState(beta.id, {}, true, &error));
    QVERIFY(!repository.historyIncomplete(std::nullopt, &error));
    QVERIFY(!repository.historyIncomplete(categoryId, &error));
}


void RepositoryTest::appliesWatchProgressAndSurvivesPruning()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));

    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos(
        {makeVideo(QStringLiteral("old"), alpha.id, now, false, 600)}, &error));

    const std::optional<Video> stored = repository.video(QStringLiteral("old"), &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    QCOMPARE(stored->id, QStringLiteral("old"));
    QCOMPARE(stored->channelId, alpha.id);
    QCOMPARE(stored->durationSeconds, 600);
    QCOMPARE(stored->isBroadcast, false);
    QVERIFY(!repository.video(QStringLiteral("missing"), &error).has_value());
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!repository.watchStats(QStringLiteral("missing"), &error).has_value());

    QVERIFY2(repository.applyWatchProgress(
                 QStringLiteral("old"), 120, 300, true, &error),
             qPrintable(error));
    std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("old"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->videoId, QStringLiteral("old"));
    QCOMPARE(stats->watchedSeconds, 120);
    QCOMPARE(stats->lastPositionSeconds, 300);
    QCOMPARE(stats->watchCount, 1);
    QVERIFY(stats->lastWatchedAt.isValid());

    QList<Video> feedVideos = repository.feed();
    QCOMPARE(feedVideos.size(), 1);
    QCOMPARE(feedVideos.first().watchProgressPercent, 50);

    QVERIFY2(repository.applyWatchProgress(QStringLiteral("old"), 60, 350, false, &error),
             qPrintable(error));
    stats = repository.watchStats(QStringLiteral("old"), &error);
    QCOMPARE(stats->watchedSeconds, 180);
    QCOMPARE(stats->lastPositionSeconds, 350);
    QCOMPARE(stats->watchCount, 1);

    feedVideos = repository.feed();
    QCOMPARE(feedVideos.first().watchProgressPercent, 58);

    // The caller owns position monotonicity; the repository stores what it gets.
    QVERIFY2(repository.applyWatchProgress(QStringLiteral("old"), 30, 100, false, &error),
             qPrintable(error));
    stats = repository.watchStats(QStringLiteral("old"), &error);
    QCOMPARE(stats->watchedSeconds, 210);
    QCOMPARE(stats->lastPositionSeconds, 100);
}

void RepositoryTest::prunesOldestVideosWhenOverStorageLimit()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    const qint64 baseSize = repository.databaseSizeBytes();

    // Enough videos with long titles to push the database past a small limit.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<Video> videos;
    const QString padding(600, QLatin1Char('x'));
    for (int index = 0; index < 120; ++index) {
        Video video = makeVideo(
            QStringLiteral("video-%1").arg(index, 3, 10, QLatin1Char('0')),
            alpha.id,
            now.addSecs(-index));
        video.title = padding;
        videos.append(video);
    }
    QVERIFY(repository.upsertVideos(videos, &error));

    const qint64 fullSize = repository.databaseSizeBytes();
    const qint64 maximumSize = baseSize + (fullSize - baseSize) / 2;
    QVERIFY(fullSize > maximumSize);
    QVERIFY(repository.pruneVideoMetadataToLimit(maximumSize, &error));
    QVERIFY(repository.databaseSizeBytes() <= maximumSize);

    // FIFO: the oldest published videos are gone, the newest survive.
    QVERIFY(!repository.video(QStringLiteral("video-119"), &error).has_value());
    QVERIFY(repository.video(QStringLiteral("video-000"), &error).has_value());

    // Pruning again below the limit is a no-op.
    const QList<Video> feedBefore = repository.feed(std::nullopt, 180, 500, &error);
    QVERIFY(repository.pruneVideoMetadataToLimit(maximumSize, &error));
    QCOMPARE(repository.feed(std::nullopt, 180, 500, &error).size(), feedBefore.size());

    // Watch data is keyed independently of cached video metadata.
    QVERIFY2(repository.applyWatchProgress(QStringLiteral("video-119"), 90, 120, true, &error),
             qPrintable(error));
    QVERIFY(repository.pruneVideoMetadataToLimit(1024, &error));
    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("video-119"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 90);
    QCOMPARE(repository.feed().size(), 0);
}

void RepositoryTest::watchHistoryInsertsAndOrders()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos({
        makeVideo(QStringLiteral("vid-a"), alpha.id, now, false, 600),
        makeVideo(QStringLiteral("vid-b"), beta.id, now, false, 300),
    }, &error));

    // Repeated counted sessions for one video stay as separate rows. The
    // newest insertion wins the (id) tiebreak when timestamps collide.
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-a"), 5, 120, true, &error));
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-a"), 5, 240, true, &error));
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-b"), 5, 150, true, &error));

    const QList<HistoryEntry> entries = repository.watchHistory(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).videoId, QStringLiteral("vid-b"));
    QCOMPARE(entries.at(0).channelTitle, QStringLiteral("Beta"));
    QCOMPARE(entries.at(0).watchProgressPercent, 50);
    QCOMPARE(entries.at(1).videoId, QStringLiteral("vid-a"));
    QCOMPARE(entries.at(1).channelTitle, QStringLiteral("Alpha"));
    QCOMPARE(entries.at(1).watchProgressPercent, 40);
    QCOMPARE(entries.at(2).videoId, QStringLiteral("vid-a"));
    QCOMPARE(entries.at(2).channelTitle, QStringLiteral("Alpha"));
    QVERIFY(entries.at(0).watchedAt.isValid());
}

void RepositoryTest::watchHistoryIgnoresUncountedSessions()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos(
        {makeVideo(QStringLiteral("vid-a"), alpha.id, now, false, 600)}, &error));

    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-a"), 30, 100, false, &error));
    QVERIFY(repository.watchHistory(&error).isEmpty());

    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("vid-a"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 30);
    QCOMPARE(stats->watchCount, 0);
}

void RepositoryTest::watchHistorySkipsMissingVideoMetadata()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));

    // A counted session for a video with no cached metadata still records
    // watch time but cannot produce a stable history row.
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("nocache"), 30, 100, true, &error));
    QVERIFY(repository.watchHistory(&error).isEmpty());

    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("nocache"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 30);
    QCOMPARE(stats->watchCount, 1);
}

void RepositoryTest::deleteWatchHistoryRemovesOnlyHistory()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    const Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    QVERIFY(repository.upsertChannel(alpha, &error));
    QVERIFY(repository.upsertChannel(beta, &error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.upsertVideos({
        makeVideo(QStringLiteral("vid-a"), alpha.id, now, false, 600),
        makeVideo(QStringLiteral("vid-b"), beta.id, now, false, 300),
    }, &error));

    // Two counted sessions for vid-a plus one for vid-b.
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-a"), 5, 120, true, &error));
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-a"), 5, 240, true, &error));
    QVERIFY(repository.applyWatchProgress(
        QStringLiteral("vid-b"), 5, 150, true, &error));
    QCOMPARE(repository.watchHistory(&error).size(), 3);

    // Empty ids are rejected and leave the table untouched.
    QVERIFY(!repository.deleteWatchHistory(QString(), &error));
    QCOMPARE(error, QStringLiteral("Video id is required to delete watch history."));
    error.clear();
    QCOMPARE(repository.watchHistory(&error).size(), 3);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // Deleting vid-a removes every one of its history rows, not just the first.
    QVERIFY(repository.deleteWatchHistory(QStringLiteral("vid-a"), &error));
    const QList<HistoryEntry> remaining = repository.watchHistory(&error);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().videoId, QStringLiteral("vid-b"));

    // The video stays in the feed and its watch progress is preserved.
    QVERIFY(repository.video(QStringLiteral("vid-a"), &error).has_value());
    const QList<Video> feedVideos = repository.feed();
    QCOMPARE(feedVideos.size(), 2);
    const std::optional<WatchStats> stats = repository.watchStats(QStringLiteral("vid-a"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 10);
    QCOMPARE(stats->watchCount, 2);
}

void RepositoryTest::modelsExposeExpectedRoles()
{
    CategoryModel categoryModel;
    QSignalSpy categoryReset(&categoryModel, &QAbstractItemModel::modelReset);
    categoryModel.setCategories({{7, QStringLiteral("Science")}});
    QCOMPARE(categoryReset.count(), 1);
    QCOMPARE(categoryModel.rowCount(), 1);
    QCOMPARE(categoryModel.data(categoryModel.index(0), CategoryModel::CategoryIdRole).toLongLong(), 7);
    QCOMPARE(categoryModel.data(categoryModel.index(0), CategoryModel::NameRole).toString(),
             QStringLiteral("Science"));

    FeedModel feedModel;
    const Video video = makeVideo(
        QStringLiteral("abc123"),
        QStringLiteral("UCAlpha"),
        QDateTime::currentDateTimeUtc());
    feedModel.setVideos({video});
    QCOMPARE(feedModel.rowCount(), 1);
    QCOMPARE(feedModel.data(feedModel.index(0), FeedModel::TitleRole).toString(), video.title);
    QCOMPARE(feedModel.data(feedModel.index(0), FeedModel::VideoUrlRole).toUrl().toString(),
             QStringLiteral("https://www.youtube.com/watch?v=abc123"));
    QCOMPARE(feedModel.data(feedModel.index(0), FeedModel::WatchProgressPercentRole).toInt(), -1);
}

void RepositoryTest::historyModelExposesExpectedRoles()
{
    HistoryModel historyModel;
    QSignalSpy reset(&historyModel, &QAbstractItemModel::modelReset);
    const QDateTime watchedAt = QDateTime::currentDateTimeUtc();
    historyModel.setEntries({
        {
            QStringLiteral("vid-a"),
            QStringLiteral("UCAlpha"),
            QStringLiteral("Alpha"),
            QStringLiteral("Video A"),
            watchedAt,
            40,
        },
    });
    QCOMPARE(reset.count(), 1);
    QCOMPARE(historyModel.rowCount(), 1);
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::VideoIdRole).toString(),
             QStringLiteral("vid-a"));
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::ChannelIdRole).toString(),
             QStringLiteral("UCAlpha"));
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::ChannelTitleRole).toString(),
             QStringLiteral("Alpha"));
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::TitleRole).toString(),
             QStringLiteral("Video A"));
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::WatchedAtRole).toDateTime(),
             watchedAt);
    QCOMPARE(historyModel.data(historyModel.index(0), HistoryModel::WatchProgressPercentRole).toInt(),
             40);
}

void RepositoryTest::watchNextModelExposesExpectedRoles()
{
    WatchNextModel watchNextModel;
    QSignalSpy reset(&watchNextModel, &QAbstractItemModel::modelReset);
    const QDateTime publishedAt = QDateTime::currentDateTimeUtc();
    const QDateTime addedAt = QDateTime::currentDateTimeUtc();
    watchNextModel.setEntries({
        {
            QStringLiteral("vid-a"),
            QStringLiteral("UCAlpha"),
            QStringLiteral("Alpha"),
            QStringLiteral("Video A"),
            publishedAt,
            40,
            0,
            addedAt,
        },
    });
    QCOMPARE(reset.count(), 1);
    QCOMPARE(watchNextModel.rowCount(), 1);
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::VideoIdRole).toString(),
             QStringLiteral("vid-a"));
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::ChannelIdRole).toString(),
             QStringLiteral("UCAlpha"));
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::ChannelTitleRole).toString(),
             QStringLiteral("Alpha"));
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::TitleRole).toString(),
             QStringLiteral("Video A"));
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::PublishedAtRole).toDateTime(),
             publishedAt);
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::WatchProgressPercentRole).toInt(),
             40);
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::PositionRole).toInt(), 0);
    QCOMPARE(watchNextModel.data(watchNextModel.index(0), WatchNextModel::AddedAtRole).toDateTime(),
             addedAt);
}

QTEST_GUILESS_MAIN(RepositoryTest)

#include "repository_test.moc"
