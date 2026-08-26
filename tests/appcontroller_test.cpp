#include "appcontroller.h"
#include "models/historymodel.h"
#include "playbacksettings.h"
#include "repository.h"

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class AppControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void normalizesBackendAndHeight();
    void formatsHeightSelector();
    void defaultsToIframeAndAutoHeight();
    void persistsMaximumHeightAcrossInstances();
    void rejectsInvalidHeightAndBackend();
    void rejectsUnavailableMpvBackend();
    void opensValidVideo();
    void rejectsInvalidVideo();
    void changesVideoWithoutReopeningPlayer();
    void closesPlayer();
    void countsWatchTimeWhilePlaying();
    void ignoresSeeksGapsAndStaleReports();
    void resumesFromStoredPosition();
    void loadMoreHistoryAppendsCachedPages();
    void reloadsWatchHistoryIntoModel();
    void deletesWatchHistoryFromModel();

private:
    QTemporaryDir m_settingsDirectory;
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
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("OmaTubeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("appcontroller_tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        m_settingsDirectory.path());
}

void AppControllerTest::init()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void AppControllerTest::normalizesBackendAndHeight()
{
    QCOMPARE(PlaybackSettings::normalizeBackend(QStringLiteral("mpv")), QStringLiteral("mpv"));
    QCOMPARE(PlaybackSettings::normalizeBackend(QStringLiteral("iframe")), QStringLiteral("iframe"));
    QCOMPARE(PlaybackSettings::normalizeBackend(QStringLiteral("MPV")), QStringLiteral("mpv"));
    QCOMPARE(PlaybackSettings::normalizeBackend(QStringLiteral("bogus")), QStringLiteral("iframe"));
    QCOMPARE(PlaybackSettings::normalizeBackend(QString()), QStringLiteral("iframe"));

    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(0), 0);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(2160), 2160);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(1440), 1440);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(1080), 1080);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(720), 720);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(480), 480);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(360), 360);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(-5), 0);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(500), 0);
    QCOMPARE(PlaybackSettings::normalizeMaximumVideoHeight(999), 0);
}

void AppControllerTest::formatsHeightSelector()
{
    QCOMPARE(PlaybackSettings::ytDlpFormatForMaximumHeight(0), QString());
    QCOMPARE(
        PlaybackSettings::ytDlpFormatForMaximumHeight(720),
        QStringLiteral("bestvideo*[height<=720]+bestaudio/best[height<=720]"));
    QCOMPARE(
        PlaybackSettings::ytDlpFormatForMaximumHeight(1080),
        QStringLiteral("bestvideo*[height<=1080]+bestaudio/best[height<=1080]"));
    QCOMPARE(PlaybackSettings::ytDlpFormatForMaximumHeight(999), QString());
}

void AppControllerTest::defaultsToIframeAndAutoHeight()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QCOMPARE(controller->videoBackend(), QStringLiteral("iframe"));
    QCOMPARE(controller->maximumVideoHeight(), 0);
}

void AppControllerTest::persistsMaximumHeightAcrossInstances()
{
    QString error;
    {
        std::unique_ptr<AppController> first =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(first->initialize(&error), qPrintable(error));
        QSignalSpy heightChanged(first.get(), &AppController::maximumVideoHeightChanged);
        first->setMaximumVideoHeight(2160);
        QCOMPARE(first->maximumVideoHeight(), 2160);
        QCOMPARE(heightChanged.count(), 1);
        QCOMPARE(
            QSettings().value(QString::fromLatin1("playback/maximumVideoHeight")).toInt(),
            2160);
    }
    {
        std::unique_ptr<AppController> second =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(second->initialize(&error), qPrintable(error));
        QCOMPARE(second->maximumVideoHeight(), 2160);
    }
}

void AppControllerTest::rejectsInvalidHeightAndBackend()
{
    QString error;
    QSettings settings;
    {
        std::unique_ptr<AppController> controller =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(controller->initialize(&error), qPrintable(error));

        controller->setMaximumVideoHeight(1080);
        QCOMPARE(controller->maximumVideoHeight(), 1080);
        QSignalSpy heightChanged(controller.get(), &AppController::maximumVideoHeightChanged);
        controller->setMaximumVideoHeight(500);
        QCOMPARE(controller->maximumVideoHeight(), 0);
        QCOMPARE(heightChanged.count(), 1);
        QCOMPARE(
            settings.value(QString::fromLatin1("playback/maximumVideoHeight")).toInt(),
            0);

        controller->setVideoBackend(QStringLiteral("bogus"));
        QCOMPARE(controller->videoBackend(), QStringLiteral("iframe"));
        QVERIFY(!settings.contains(QString::fromLatin1("playback/backend")));
    }
    {
        settings.setValue(QString::fromLatin1("playback/backend"), QStringLiteral("bogus"));
        settings.sync();
        std::unique_ptr<AppController> reloaded =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(reloaded->initialize(&error), qPrintable(error));
        QCOMPARE(reloaded->videoBackend(), QStringLiteral("iframe"));
    }
}

void AppControllerTest::rejectsUnavailableMpvBackend()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    if (controller->mpvAvailable())
        QSKIP("Embedded mpv is available in this build; rejection does not apply.");

    controller->setVideoBackend(QStringLiteral("mpv"));
    QCOMPARE(controller->videoBackend(), QStringLiteral("iframe"));
    QCOMPARE(controller->errorMessage(), QStringLiteral("Embedded mpv is unavailable in this build."));
    QVERIFY(!QSettings().contains(QString::fromLatin1("playback/backend")));
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

void AppControllerTest::loadMoreHistoryAppendsCachedPages()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    {
        Repository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        const Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
        QVERIFY(repository.upsertChannel(alpha, &error));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QList<Video> videos;
        for (int index = 0; index < 75; ++index) {
            videos.append(makeVideo(
                QStringLiteral("video-%1").arg(index, 3, 10, QLatin1Char('0')),
                alpha.id,
                now.addSecs(-index)));
        }
        QVERIFY(repository.upsertVideos(videos, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    FeedModel *feed = controller->feed();
    QCOMPARE(feed->rowCount(), 50);

    QSignalSpy rowsInserted(feed, &QAbstractItemModel::rowsInserted);
    QSignalSpy modelReset(feed, &QAbstractItemModel::modelReset);
    controller->loadMoreHistory();
    QCOMPARE(feed->rowCount(), 75);
    QCOMPARE(rowsInserted.count(), 1);
    QCOMPARE(modelReset.count(), 0);
    QVERIFY(!controller->historyLoading());

    // Local cache is exhausted; further loads are no-ops without a key.
    controller->loadMoreHistory();
    QCOMPARE(feed->rowCount(), 75);
    QCOMPARE(rowsInserted.count(), 1);
}

void AppControllerTest::reloadsWatchHistoryIntoModel()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    {
        Repository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(
            makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(repository.upsertVideos(
            {makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now)},
            &error));
        QVERIFY(repository.applyWatchProgress(
            QStringLiteral("dQw4w9WgXcQ"), 30, 100, true, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    HistoryModel *history = controller->watchHistory();
    QVERIFY(history != nullptr);
    QCOMPARE(history->rowCount(), 0);

    QSignalSpy modelReset(history, &QAbstractItemModel::modelReset);
    controller->reloadWatchHistory();
    QCOMPARE(history->rowCount(), 1);
    QCOMPARE(modelReset.count(), 1);
    QCOMPARE(history->data(history->index(0), HistoryModel::TitleRole).toString(),
             QStringLiteral("Video dQw4w9WgXcQ"));
    QCOMPARE(history->data(history->index(0), HistoryModel::ChannelTitleRole).toString(),
             QStringLiteral("Alpha"));
    QCOMPARE(history->data(history->index(0), HistoryModel::WatchProgressPercentRole).toInt(),
             16);
    QVERIFY(controller->errorMessage().isEmpty());
}

void AppControllerTest::deletesWatchHistoryFromModel()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    {
        Repository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(
            makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(repository.upsertVideos(
            {makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now)},
            &error));
        // Two counted sessions, plus a second video that must survive.
        QVERIFY(repository.applyWatchProgress(
            QStringLiteral("dQw4w9WgXcQ"), 30, 100, true, &error));
        QVERIFY(repository.applyWatchProgress(
            QStringLiteral("dQw4w9WgXcQ"), 10, 200, true, &error));
        QVERIFY(repository.upsertVideos(
            {makeVideo(QStringLiteral("BBBBBBBBBBB"), QStringLiteral("UCAlpha"), now, false, 300)},
            &error));
        QVERIFY(repository.applyWatchProgress(
            QStringLiteral("BBBBBBBBBBB"), 20, 60, true, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->reloadWatchHistory();
    HistoryModel *history = controller->watchHistory();
    QCOMPARE(history->rowCount(), 3);

    QSignalSpy modelReset(history, &QAbstractItemModel::modelReset);
    QVERIFY(controller->deleteWatchHistory(QStringLiteral("dQw4w9WgXcQ")));
    QCOMPARE(history->rowCount(), 1);
    QCOMPARE(modelReset.count(), 1);
    QCOMPARE(history->data(history->index(0), HistoryModel::VideoIdRole).toString(),
             QStringLiteral("BBBBBBBBBBB"));
    QVERIFY(controller->errorMessage().isEmpty());

    // Feed video and watch progress survive deletion of its history rows.
    QCOMPARE(controller->feed()->rowCount(), 2);
    const QVariantMap stats = controller->watchStatsForVideo(QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(stats.value(QStringLiteral("watchedSeconds")).toLongLong(), 40);
    QCOMPARE(stats.value(QStringLiteral("watchCount")).toInt(), 2);
}

QTEST_GUILESS_MAIN(AppControllerTest)

#include "appcontroller_test.moc"
