#include "models/categorymodel.h"
#include "models/feedmodel.h"
#include "repository.h"

#include <QSignalSpy>
#include <QTest>

class RepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void categoryLifecycle();
    void categoryMembershipFiltersChannels();
    void feedExcludesBroadcastsAndFiltersCategories();
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
    bool isBroadcast = false)
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
    };
}
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

void RepositoryTest::feedExcludesBroadcastsAndFiltersCategories()
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
        makeVideo(QStringLiteral("old"), alpha.id, now.addSecs(-60)),
        makeVideo(QStringLiteral("new"), beta.id, now),
        makeVideo(QStringLiteral("live"), alpha.id, now.addSecs(60), true),
    }, &error));

    const QList<Video> allFeed = repository.feed();
    QCOMPARE(allFeed.size(), 2);
    QCOMPARE(allFeed.first().id, QStringLiteral("new"));
    QCOMPARE(allFeed.last().channelTitle, QStringLiteral("Alpha"));

    const QList<Video> selectedFeed = repository.feed(categoryId);
    QCOMPARE(selectedFeed.size(), 1);
    QCOMPARE(selectedFeed.first().id, QStringLiteral("old"));
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
}

QTEST_GUILESS_MAIN(RepositoryTest)

#include "repository_test.moc"
