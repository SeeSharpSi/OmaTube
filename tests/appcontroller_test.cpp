#include "appcontroller.h"
#include "repository.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void opensValidVideo();
    void rejectsInvalidVideo();
    void changesVideoWithoutReopeningPlayer();
    void closesPlayer();
    void countsWatchTimeWhilePlaying();
    void ignoresSeeksGapsAndStaleReports();
    void resumesFromStoredPosition();
};

namespace {
constexpr auto videoA = "AAAAAAAAAAA";
constexpr auto videoB = "BBBBBBBBBBB";

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

void AppControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AppControllerTest::opensValidVideo()
{
    std::unique_ptr<AppController> controller = AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QSignalSpy videoIdChanged(controller.get(), &AppController::currentVideoIdChanged);
    QSignalSpy playerOpenChanged(controller.get(), &AppController::playerOpenChanged);

    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));

    QCOMPARE(controller->currentVideoId(), QStringLiteral("dQw4w9WgXcQ"));
    QVERIFY(controller->playerOpen());
    QCOMPARE(videoIdChanged.count(), 1);
    QCOMPARE(playerOpenChanged.count(), 1);
}

void AppControllerTest::rejectsInvalidVideo()
{
    std::unique_ptr<AppController> controller = AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QSignalSpy videoIdChanged(controller.get(), &AppController::currentVideoIdChanged);
    QSignalSpy playerOpenChanged(controller.get(), &AppController::playerOpenChanged);

    controller->openVideo(QStringLiteral("invalid!id"));

    QCOMPARE(controller->errorMessage(), QStringLiteral("Video URL is invalid."));
    QVERIFY(controller->currentVideoId().isEmpty());
    QVERIFY(!controller->playerOpen());
    QCOMPARE(videoIdChanged.count(), 0);
    QCOMPARE(playerOpenChanged.count(), 0);
}

void AppControllerTest::changesVideoWithoutReopeningPlayer()
{
    std::unique_ptr<AppController> controller = AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));
    QSignalSpy videoIdChanged(controller.get(), &AppController::currentVideoIdChanged);
    QSignalSpy playerOpenChanged(controller.get(), &AppController::playerOpenChanged);

    controller->openVideo(QStringLiteral("ABCdef12345"));

    QCOMPARE(controller->currentVideoId(), QStringLiteral("ABCdef12345"));
    QVERIFY(controller->playerOpen());
    QCOMPARE(videoIdChanged.count(), 1);
    QCOMPARE(playerOpenChanged.count(), 0);
}

void AppControllerTest::closesPlayer()
{
    std::unique_ptr<AppController> controller = AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));
    QSignalSpy playerOpenChanged(controller.get(), &AppController::playerOpenChanged);

    controller->closePlayer();

    QVERIFY(!controller->playerOpen());
    QCOMPARE(controller->currentVideoId(), QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(playerOpenChanged.count(), 1);
}

void AppControllerTest::countsWatchTimeWhilePlaying()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));

    const QString videoId = QString::fromUtf8(videoA);
    controller->openVideo(videoId);
    QCOMPARE(controller->currentStartPosition(), 0);

    controller->reportPlayback(videoId, 0.0, false);
    controller->reportPlayback(videoId, 5.0, true);
    controller->reportPlayback(videoId, 10.0, true);
    controller->reportPlayback(videoId, 15.0, true);
    controller->closePlayer();

    QVariantMap stats = controller->watchStatsForVideo(videoId);
    QCOMPARE(stats.value(QStringLiteral("watchedSeconds")).toLongLong(), 15);
    QCOMPARE(stats.value(QStringLiteral("lastPositionSeconds")).toInt(), 15);
    QCOMPARE(stats.value(QStringLiteral("watchCount")).toInt(), 1);
    QVERIFY(stats.value(QStringLiteral("lastWatchedAt")).toDateTime().isValid());

    // Paused time between playing reports is not credited.
    controller->openVideo(videoId);
    controller->reportPlayback(videoId, 15.0, true);
    controller->reportPlayback(videoId, 15.0, false);
    controller->reportPlayback(videoId, 16.0, true);
    controller->closePlayer();

    stats = controller->watchStatsForVideo(videoId);
    QCOMPARE(stats.value(QStringLiteral("watchedSeconds")).toLongLong(), 16);
    QCOMPARE(stats.value(QStringLiteral("lastPositionSeconds")).toInt(), 16);
}

void AppControllerTest::ignoresSeeksGapsAndStaleReports()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));

    const QString first = QString::fromUtf8(videoA);
    controller->openVideo(first);
    controller->reportPlayback(first, 0.0, true);
    // Position jump far beyond one poll interval looks like a seek: resync, no credit.
    controller->reportPlayback(first, 200.0, true);
    // Backwards movement is never credited.
    controller->reportPlayback(first, 195.0, true);
    // Sub-second deltas round down to zero credit.
    controller->reportPlayback(first, 195.4, true);
    controller->reportPlayback(first, 195.6, true);

    // Switching videos makes further reports for the old id stale.
    const QString second = QString::fromUtf8(videoB);
    controller->openVideo(second);
    controller->reportPlayback(first, 300.0, true);
    controller->closePlayer();

    QVERIFY(controller->watchStatsForVideo(first).isEmpty());
    QVERIFY(controller->watchStatsForVideo(second).isEmpty());
}

void AppControllerTest::resumesFromStoredPosition()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));

    {
        Repository seedRepository(databasePath);
        QString seedError;
        QVERIFY2(seedRepository.open(&seedError), qPrintable(seedError));
        QVERIFY(seedRepository.upsertChannel(
            makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &seedError));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(seedRepository.upsertVideos(
            {makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now)},
            &seedError));
        QVERIFY(seedRepository.upsertVideos(
            {makeVideo(
                QStringLiteral("broadcast12"),
                QStringLiteral("UCAlpha"),
                now,
                true,
                -1)},
            &seedError));
        QVERIFY(seedRepository.upsertVideos(
            {makeVideo(
                QStringLiteral("shortvideo1"),
                QStringLiteral("UCAlpha"),
                now,
                false,
                240)},
            &seedError));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));

    const QString videoId = QStringLiteral("dQw4w9WgXcQ");
    controller->openVideo(videoId);
    QCOMPARE(controller->currentStartPosition(), 0);
    controller->reportPlayback(videoId, 100.0, false);
    controller->reportPlayback(videoId, 105.0, true);
    controller->reportPlayback(videoId, 110.0, true);
    controller->closePlayer();

    const QVariantMap stats = controller->watchStatsForVideo(videoId);
    QCOMPARE(stats.value(QStringLiteral("watchedSeconds")).toLongLong(), 10);
    QCOMPARE(stats.value(QStringLiteral("watchCount")).toInt(), 1);

    controller->openVideo(videoId);
    QCOMPARE(controller->currentStartPosition(), 110);

    // Watching to within the end threshold marks the video finished.
    controller->reportPlayback(videoId, 540.0, false);
    controller->reportPlayback(videoId, 555.0, true);
    controller->closePlayer();

    controller->openVideo(videoId);
    QCOMPARE(controller->currentStartPosition(), 0);
    controller->closePlayer();

    // Broadcasts always restart from the live edge.
    const QString broadcastId = QStringLiteral("broadcast12");
    controller->openVideo(broadcastId);
    QCOMPARE(controller->currentStartPosition(), 0);
    controller->reportPlayback(broadcastId, 50.0, false);
    controller->reportPlayback(broadcastId, 60.0, true);
    controller->closePlayer();

    controller->openVideo(broadcastId);
    QCOMPARE(controller->currentStartPosition(), 0);
    controller->closePlayer();
}

QTEST_GUILESS_MAIN(AppControllerTest)

#include "appcontroller_test.moc"
