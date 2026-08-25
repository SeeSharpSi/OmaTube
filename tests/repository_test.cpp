#include "models/categorymodel.h"
#include "models/feedmodel.h"
#include "repository.h"

#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class RepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void persistentDatabaseOpens();
    void migratesVersionOneDatabase();
    void migratesVersionTwoDatabase();
    void categoryLifecycle();
    void categoryMembershipFiltersChannels();
    void feedExcludesBroadcastsShortVideosAndFiltersCategories();
    void appliesWatchProgressAndSurvivesPruning();
    void modelsExposeExpectedRoles();
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
    QCOMPARE(repository.feed().size(), 0);
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

void RepositoryTest::categoryLifecycle()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));

    const qint64 newsId = repository.addCategory(QStringLiteral(" News "), &error);
    QVERIFY2(newsId > 0, qPrintable(error));
    const qint64 musicId = repository.addCategory(QStringLiteral("Music"), &error);
    QVERIFY2(musicId > 0, qPrintable(error));

    QCOMPARE(repository.categories(), QList<Category>({{newsId, QStringLiteral("News")},
                                                        {musicId, QStringLiteral("Music")}}));
    QVERIFY(repository.renameCategory(newsId, QStringLiteral("Current events"), &error));
    QCOMPARE(repository.categories().first().name, QStringLiteral("Current events"));
    QVERIFY(repository.removeCategory(musicId, &error));
    QCOMPARE(repository.categories().size(), 1);
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

    QVERIFY2(repository.pruneVideoMetadata(now.addSecs(3600), &error), qPrintable(error));
    QVERIFY(repository.feed().isEmpty());
    QVERIFY(!repository.video(QStringLiteral("old"), &error).has_value());

    // Watch data is keyed independently of cached video metadata.
    stats = repository.watchStats(QStringLiteral("old"), &error);
    QVERIFY2(stats.has_value(), qPrintable(error));
    QCOMPARE(stats->watchedSeconds, 210);
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

QTEST_GUILESS_MAIN(RepositoryTest)

#include "repository_test.moc"
