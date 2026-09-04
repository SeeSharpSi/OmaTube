#include "appcontroller.h"
#include "models/historymodel.h"
#include "models/watchnextmodel.h"
#include "playbacksettings.h"
#include "repository.h"
#include "spaceholdhandler.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
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
    void automationModeDefaultsToFalse();
    void automationModeSuppressesNetworkRefresh();
    void automationModeAddChannelIsDisabled();
    void persistsMaximumHeightAcrossInstances();
    void rejectsInvalidHeightAndBackend();
    void rejectsUnavailableMpvBackend();
    void opensValidVideo();
    void rejectsInvalidVideo();
    void changesVideoWithoutReopeningPlayer();
    void changesVideoUsesNewResumePosition();
    void closesPlayer();
    void countsWatchTimeWhilePlaying();
    void ignoresSeeksGapsAndStaleReports();
    void resumesFromStoredPosition();
    void loadMoreHistoryAppendsCachedPages();
    void reloadsWatchHistoryIntoModel();
    void deletesWatchHistoryFromModel();
    void watchNextLifecycleThroughController();
    void playbackVolumeDefaultsTo100();
    void playbackVolumeClampingAndPersistence();
    void simpleUiPersistence();
    void perVideoHeightFallsBackToGlobal();
    void perVideoHeightOverrideIsolation();
    void perVideoHeightPersistenceAndDefaultRemoval();
    void perVideoHeightGlobalChangeRespectsOverride();
    void currentVideoTitleFromRepository();
    void currentVideoTitleClearsForUnknownVideo();
    void movesCategoriesAndPersists();
    void exportsAndImportsChannels();
    void exportsAndImportsCategories();
    void rejectsInvalidChannelImportBeforeWrites();
    void keybindsFooterTextOrdering();
    void spaceHoldShortPressEmitsTappedOnly();
    void spaceHoldLongPressTransitionsHeld();
    void spaceHoldAutorepeatIgnored();
    void spaceHoldDeactivationClearsHeldWithoutTap();
    void errorNotificationsCreatesWithSafeDefaults();
    void errorNotificationsInitialMessageBecomesNotification();
    void errorNotificationsIgnoresEmptyMessage();
    void errorNotificationsDeduplicatesExactMessages();
    void errorNotificationsMaximumVisibleEvictsOldest();
    void errorNotificationsDismissRemovesExactlyOne();
    void errorNotificationsDismissInvalidIndexIsHarmless();
    void errorNotificationsClearRemovesAllWithoutDismissedSignal();
    void errorNotificationsTimeoutAutoDismisses();
    void errorNotificationsHeightGrowsWithDelegates();
    void errorNotificationsLeftClickCopiesMessageAndKeepsCard();
    void errorNotificationsRightClickDismissesCard();
    void errorNotificationsCloseButtonDismissesCard();

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

std::unique_ptr<QObject> createErrorNotifications(
    QQmlEngine *engine,
    QString *errorString,
    const QVariantMap &initialProperties = {})
{
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qml/ErrorNotifications.qml")));
    if (component.isError()) {
        if (errorString)
            *errorString = component.errorString();
        return nullptr;
    }
    std::unique_ptr<QObject> object(component.createWithInitialProperties(initialProperties));
    if (component.isError()) {
        if (errorString)
            *errorString = component.errorString();
        return nullptr;
    }
    return object;
}

bool invokePushError(QObject *target, const QString &message)
{
    return QMetaObject::invokeMethod(target, "pushError", Q_ARG(QVariant, message));
}

bool invokeDismiss(QObject *target, int index)
{
    return QMetaObject::invokeMethod(target, "dismiss", Q_ARG(QVariant, index));
}

bool invokeClear(QObject *target)
{
    return QMetaObject::invokeMethod(target, "clear");
}

// Repeater delegates are JS-owned with no QObject parent, so QObject-based
// findChild() cannot see them; search the visual item tree instead.
QList<QQuickItem *> findVisualChildrenByName(QQuickItem *parent, const QString &name)
{
    QList<QQuickItem *> matches;
    if (!parent)
        return matches;
    if (parent->objectName() == name)
        matches.append(parent);
    const QList<QQuickItem *> children = parent->childItems();
    for (QQuickItem *child : children)
        matches.append(findVisualChildrenByName(child, name));
    return matches;
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

void AppControllerTest::automationModeDefaultsToFalse()
{
    QString error;
    {
        std::unique_ptr<AppController> normal =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(normal->initialize(&error), qPrintable(error));
        QVERIFY(!normal->automationMode());
    }

    std::unique_ptr<AppController> automated =
        AppController::createApplication(QStringLiteral(":memory:"), true);
    QVERIFY2(automated->initialize(&error), qPrintable(error));
    QVERIFY(automated->automationMode());
}

void AppControllerTest::automationModeSuppressesNetworkRefresh()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"), true);
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QVERIFY(controller->automationMode());

    controller->startupRefresh();
    controller->refresh();
    QVERIFY(!controller->refreshing());
    QVERIFY(!controller->historyLoading());
    QCOMPARE(controller->statusMessage(), QStringLiteral("Automation mode: refresh is disabled."));

    controller->loadMoreHistory();
    QVERIFY(!controller->historyLoading());
    QVERIFY(!controller->refreshing());
}

void AppControllerTest::automationModeAddChannelIsDisabled()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"), true);
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));

    controller->addChannel(QStringLiteral("https://youtube.com/@someone"), {});
    QVERIFY(!controller->addingChannel());
    QCOMPARE(controller->errorMessage(), QStringLiteral("Automation mode: adding channels is disabled."));
    QCOMPARE(controller->channels()->rowCount(), 0);
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

void AppControllerTest::changesVideoUsesNewResumePosition()
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
            {makeVideo(QString::fromUtf8(videoA), QStringLiteral("UCAlpha"), now),
             makeVideo(QString::fromUtf8(videoB), QStringLiteral("UCAlpha"), now)},
            &error));
        QVERIFY(repository.applyWatchProgress(
            QString::fromUtf8(videoB), 1, 75, true, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->openVideo(QString::fromUtf8(videoA));
    QCOMPARE(controller->currentStartPosition(), 0);

    controller->openVideo(QString::fromUtf8(videoB));
    QVERIFY(controller->playerOpen());
    QCOMPARE(controller->currentStartPosition(), 75);
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

void AppControllerTest::watchNextLifecycleThroughController()
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
            {makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now),
             makeVideo(QStringLiteral("AAAAAAAAAAA"), QStringLiteral("UCAlpha"), now)},
            &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    WatchNextModel *queue = controller->watchNext();
    QVERIFY(queue != nullptr);
    QCOMPARE(queue->rowCount(), 0);

    QVERIFY(!controller->isInWatchNext(QStringLiteral("dQw4w9WgXcQ")));
    QSignalSpy feedback(controller.get(), &AppController::watchNextFeedback);
    QVERIFY(controller->addToWatchNext(QStringLiteral("dQw4w9WgXcQ")));
    QVERIFY(controller->isInWatchNext(QStringLiteral("dQw4w9WgXcQ")));
    QCOMPARE(queue->rowCount(), 1);
    QCOMPARE(queue->data(queue->index(0), WatchNextModel::VideoIdRole).toString(),
             QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(feedback.count(), 1);
    QCOMPARE(feedback.at(0).at(0).toString(), QStringLiteral("Added to Watch Next"));

    // Re-adding is idempotent but still emits feedback every time.
    QVERIFY(controller->addToWatchNext(QStringLiteral("dQw4w9WgXcQ")));
    QCOMPARE(queue->rowCount(), 1);
    QCOMPARE(controller->statusMessage(), QStringLiteral("Already in Watch Next."));
    QCOMPARE(feedback.count(), 2);
    QCOMPARE(feedback.at(1).at(0).toString(), QStringLiteral("Already in Watch Next"));
    QVERIFY(controller->addToWatchNext(QStringLiteral("dQw4w9WgXcQ")));
    QCOMPARE(queue->rowCount(), 1);
    QCOMPARE(feedback.count(), 3);
    QCOMPARE(feedback.at(2).at(0).toString(), QStringLiteral("Already in Watch Next"));

    QVERIFY(controller->addToWatchNext(QStringLiteral("AAAAAAAAAAA")));
    QCOMPARE(queue->rowCount(), 2);
    QCOMPARE(feedback.count(), 4);
    QCOMPARE(feedback.at(3).at(0).toString(), QStringLiteral("Added to Watch Next"));
    QVERIFY(controller->moveWatchNext(QStringLiteral("AAAAAAAAAAA"), 0));
    QCOMPARE(queue->data(queue->index(0), WatchNextModel::VideoIdRole).toString(),
             QStringLiteral("AAAAAAAAAAA"));

    QVERIFY(!controller->addToWatchNext(QStringLiteral("short")));
    QVERIFY(!controller->errorMessage().isEmpty());
    QCOMPARE(feedback.count(), 4);
    controller->clearError();

    QVERIFY(controller->removeFromWatchNext(QStringLiteral("AAAAAAAAAAA")));
    QCOMPARE(queue->rowCount(), 1);
    QCOMPARE(queue->data(queue->index(0), WatchNextModel::VideoIdRole).toString(),
             QStringLiteral("dQw4w9WgXcQ"));
    QVERIFY(!controller->isInWatchNext(QStringLiteral("AAAAAAAAAAA")));
    QCOMPARE(feedback.count(), 5);
    QCOMPARE(feedback.at(4).at(0).toString(), QStringLiteral("Removed from Watch Next"));
}

void AppControllerTest::playbackVolumeDefaultsTo100()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QCOMPARE(controller->playbackVolume(), 100);
}

void AppControllerTest::playbackVolumeClampingAndPersistence()
{
    QString error;
    {
        std::unique_ptr<AppController> controller =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QSignalSpy changed(controller.get(), &AppController::playbackVolumeChanged);
        controller->setPlaybackVolume(-5);
        QCOMPARE(controller->playbackVolume(), 0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(QSettings().value(QString::fromLatin1("playback/volume")).toInt(), 0);

        // No signal when value unchanged after clamping.
        controller->setPlaybackVolume(-10);
        QCOMPARE(changed.count(), 1);

        controller->setPlaybackVolume(150);
        QCOMPARE(controller->playbackVolume(), 100);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(QSettings().value(QString::fromLatin1("playback/volume")).toInt(), 100);

        controller->setPlaybackVolume(73);
        QCOMPARE(controller->playbackVolume(), 73);
        QCOMPARE(changed.count(), 3);
        QCOMPARE(QSettings().value(QString::fromLatin1("playback/volume")).toInt(), 73);
    }
    {
        std::unique_ptr<AppController> reloaded =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(reloaded->initialize(&error), qPrintable(error));
        QCOMPARE(reloaded->playbackVolume(), 73);
    }
    // Corrupted stored value is clamped on load.
    QSettings().setValue(QString::fromLatin1("playback/volume"), 999);
    QSettings().sync();
    {
        std::unique_ptr<AppController> clamped =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(clamped->initialize(&error), qPrintable(error));
        QCOMPARE(clamped->playbackVolume(), 100);
    }
}

void AppControllerTest::simpleUiPersistence()
{
    QString error;
    {
        std::unique_ptr<AppController> controller =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QCOMPARE(controller->simpleUi(), false);

        QSignalSpy changed(controller.get(), &AppController::simpleUiChanged);
        controller->setSimpleUi(true);
        QCOMPARE(controller->simpleUi(), true);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(QSettings().value(QString::fromLatin1("appearance/simpleUi")).toBool(), true);

        // No extra signal when the value is unchanged.
        controller->setSimpleUi(true);
        QCOMPARE(changed.count(), 1);

        // Turning it back off persists and emits again.
        controller->setSimpleUi(false);
        QCOMPARE(controller->simpleUi(), false);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(QSettings().value(QString::fromLatin1("appearance/simpleUi")).toBool(), false);

        controller->setSimpleUi(true);
        QCOMPARE(changed.count(), 3);
        QCOMPARE(QSettings().value(QString::fromLatin1("appearance/simpleUi")).toBool(), true);
    }
    {
        std::unique_ptr<AppController> reloaded =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(reloaded->initialize(&error), qPrintable(error));
        QCOMPARE(reloaded->simpleUi(), true);
    }
}

void AppControllerTest::perVideoHeightFallsBackToGlobal()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->setMaximumVideoHeight(720);
    QCOMPARE(controller->maximumVideoHeight(), 720);

    const QString videoId = QStringLiteral("dQw4w9WgXcQ");
    QSignalSpy effectiveChanged(controller.get(), &AppController::currentVideoMaximumHeightChanged);
    QSignalSpy overrideChanged(controller.get(), &AppController::currentVideoMaximumHeightOverrideChanged);
    controller->openVideo(videoId);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), -1);
    QCOMPARE(controller->currentVideoMaximumHeight(), 720);
    // Opening same video with no stored override should not emit again.
    effectiveChanged.clear();
    overrideChanged.clear();
    controller->openVideo(videoId);
    QCOMPARE(effectiveChanged.count(), 0);
    QCOMPARE(overrideChanged.count(), 0);
}

void AppControllerTest::perVideoHeightOverrideIsolation()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->setMaximumVideoHeight(480);

    const QString videoAId = QStringLiteral("AAAAAAAAAAA");
    const QString videoBId = QStringLiteral("BBBBBBBBBBB");

    controller->openVideo(videoAId);
    QCOMPARE(controller->currentVideoMaximumHeight(), 480);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), -1);

    controller->setCurrentVideoMaximumHeightOverride(1080);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), 1080);
    QCOMPARE(controller->currentVideoMaximumHeight(), 1080);
    QVERIFY(QSettings().contains(QString::fromLatin1("playback/videoMaximumHeight/AAAAAAAAAAA")));
    QCOMPARE(QSettings().value(QString::fromLatin1("playback/videoMaximumHeight/AAAAAAAAAAA")).toInt(), 1080);
    QVERIFY(!QSettings().contains(QString::fromLatin1("playback/videoMaximumHeight/BBBBBBBBBBB")));

    controller->openVideo(videoBId);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), -1);
    QCOMPARE(controller->currentVideoMaximumHeight(), 480);

    controller->openVideo(videoAId);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), 1080);
    QCOMPARE(controller->currentVideoMaximumHeight(), 1080);
}

void AppControllerTest::perVideoHeightPersistenceAndDefaultRemoval()
{
    QString error;
    const QString videoId = QStringLiteral("dQw4w9WgXcQ");
    {
        std::unique_ptr<AppController> controller =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        controller->setMaximumVideoHeight(720);
        controller->openVideo(videoId);
        controller->setCurrentVideoMaximumHeightOverride(1080);
        QCOMPARE(controller->currentVideoMaximumHeight(), 1080);
        QCOMPARE(QSettings().value(QString::fromLatin1("playback/videoMaximumHeight/dQw4w9WgXcQ")).toInt(), 1080);

        QSignalSpy effectiveChanged(controller.get(), &AppController::currentVideoMaximumHeightChanged);
        QSignalSpy overrideChanged(controller.get(), &AppController::currentVideoMaximumHeightOverrideChanged);
        controller->setCurrentVideoMaximumHeightOverride(-1);
        QCOMPARE(controller->currentVideoMaximumHeightOverride(), -1);
        QCOMPARE(controller->currentVideoMaximumHeight(), 720);
        QCOMPARE(effectiveChanged.count(), 1);
        QCOMPARE(overrideChanged.count(), 1);
        QVERIFY(!QSettings().contains(QString::fromLatin1("playback/videoMaximumHeight/dQw4w9WgXcQ")));
    }
    // Override removal persists across instances, other values remain valid including 0=Auto.
    QSettings().setValue(QString::fromLatin1("playback/videoMaximumHeight/dQw4w9WgXcQ"), 0);
    QSettings().sync();
    {
        std::unique_ptr<AppController> reloaded =
            AppController::createApplication(QStringLiteral(":memory:"));
        QVERIFY2(reloaded->initialize(&error), qPrintable(error));
        reloaded->openVideo(videoId);
        QCOMPARE(reloaded->currentVideoMaximumHeightOverride(), 0);
        QCOMPARE(reloaded->currentVideoMaximumHeight(), 0);
        reloaded->setCurrentVideoMaximumHeightOverride(360);
        QCOMPARE(reloaded->currentVideoMaximumHeight(), 360);
    }
}

void AppControllerTest::perVideoHeightGlobalChangeRespectsOverride()
{
    std::unique_ptr<AppController> controller =
        AppController::createApplication(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->setMaximumVideoHeight(720);
    const QString videoId = QStringLiteral("dQw4w9WgXcQ");
    const QString otherId = QStringLiteral("AAAAAAAAAAA");
    controller->openVideo(videoId);
    QCOMPARE(controller->currentVideoMaximumHeight(), 720);

    // No override: global change updates effective.
    QSignalSpy effectiveChanged(controller.get(), &AppController::currentVideoMaximumHeightChanged);
    controller->setMaximumVideoHeight(1080);
    QCOMPARE(controller->currentVideoMaximumHeight(), 1080);
    QCOMPARE(effectiveChanged.count(), 1);

    // With override: global change must not affect effective.
    controller->setCurrentVideoMaximumHeightOverride(480);
    QCOMPARE(controller->currentVideoMaximumHeight(), 480);
    effectiveChanged.clear();
    controller->setMaximumVideoHeight(2160);
    QCOMPARE(controller->maximumVideoHeight(), 2160);
    QCOMPARE(controller->currentVideoMaximumHeight(), 480);
    QCOMPARE(effectiveChanged.count(), 0);

    // Other video without override sees new global.
    controller->openVideo(otherId);
    QCOMPARE(controller->currentVideoMaximumHeightOverride(), -1);
    QCOMPARE(controller->currentVideoMaximumHeight(), 2160);
}

void AppControllerTest::currentVideoTitleFromRepository()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    {
        Repository repo(databasePath);
        QVERIFY2(repo.open(&error), qPrintable(error));
        QVERIFY(repo.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(repo.upsertVideos({makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now)}, &error));
        QVERIFY(repo.upsertVideos({makeVideo(QStringLiteral("AAAAAAAAAAA"), QStringLiteral("UCAlpha"), now)}, &error));
        // Override title for second video
        Video second = makeVideo(QStringLiteral("AAAAAAAAAAA"), QStringLiteral("UCAlpha"), now);
        second.title = QStringLiteral("Second Video Title");
        QVERIFY(repo.upsertVideos({second}, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));

    QSignalSpy titleChanged(controller.get(), &AppController::currentVideoTitleChanged);
    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(controller->currentVideoTitle(), QStringLiteral("Video dQw4w9WgXcQ"));
    QCOMPARE(titleChanged.count(), 1);

    titleChanged.clear();
    controller->openVideo(QStringLiteral("AAAAAAAAAAA"));
    QCOMPARE(controller->currentVideoTitle(), QStringLiteral("Second Video Title"));
    QCOMPARE(titleChanged.count(), 1);

    // Switching back updates title again without reopening player effect.
    titleChanged.clear();
    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(controller->currentVideoTitle(), QStringLiteral("Video dQw4w9WgXcQ"));
    QCOMPARE(titleChanged.count(), 1);
}

void AppControllerTest::currentVideoTitleClearsForUnknownVideo()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    {
        Repository repo(databasePath);
        QVERIFY2(repo.open(&error), qPrintable(error));
        QVERIFY(repo.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(repo.upsertVideos({makeVideo(QStringLiteral("dQw4w9WgXcQ"), QStringLiteral("UCAlpha"), now)}, &error));
    }
    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    controller->openVideo(QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(controller->currentVideoTitle(), QStringLiteral("Video dQw4w9WgXcQ"));
    QSignalSpy titleChanged(controller.get(), &AppController::currentVideoTitleChanged);
    controller->openVideo(QStringLiteral("BBBBBBBBBBB"));
    QCOMPARE(controller->currentVideoTitle(), QString());
    QCOMPARE(titleChanged.count(), 1);
}

void AppControllerTest::movesCategoriesAndPersists()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = temporaryDirectory.filePath(QStringLiteral("yt-client.sqlite3"));
    QString error;
    QList<qint64> categoryIds;
    {
        Repository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        for (const QString &name : {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")})
            categoryIds.append(repository.addCategory(name, &error));
    }

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QVERIFY(controller->moveCategory(categoryIds.at(1), 0));
    QCOMPARE(controller->categories()->data(controller->categories()->index(0), CategoryModel::CategoryIdRole)
                 .toLongLong(),
             categoryIds.at(1));
    QCOMPARE(controller->categories()->data(controller->categories()->index(1), CategoryModel::CategoryIdRole)
                 .toLongLong(),
             categoryIds.at(0));
    QCOMPARE(controller->categories()->data(controller->categories()->index(2), CategoryModel::CategoryIdRole)
                 .toLongLong(),
             categoryIds.at(2));
    QVERIFY(!controller->moveCategory(categoryIds.at(1), 3));
    QVERIFY(!controller->errorMessage().isEmpty());
    controller.reset();

    Repository repository(databasePath);
    QVERIFY2(repository.open(&error), qPrintable(error));
    QCOMPARE(repository.categories().value(0).id, categoryIds.at(1));
}

void AppControllerTest::exportsAndImportsChannels()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString targetPath = directory.filePath(QStringLiteral("target.sqlite3"));
    const QString exportPath = directory.filePath(QStringLiteral("channels.json"));
    const QDateTime fetchedAt = QDateTime::fromString(
        QStringLiteral("2026-08-29T12:34:56.789Z"), Qt::ISODateWithMs);
    Channel alpha = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    alpha.originalInput = QStringLiteral("https://youtube.com/@alpha");
    alpha.handle = QStringLiteral("@alpha");
    alpha.avatarUrl = QStringLiteral("https://img/alpha.jpg");
    alpha.uploadsPlaylistId = QStringLiteral("UUALPHA");
    alpha.metadataFetchedAt = fetchedAt;
    Channel beta = makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta"));
    beta.originalInput = QStringLiteral("@beta");
    beta.handle = QStringLiteral("@beta");
    beta.avatarUrl = QStringLiteral("https://img/beta.jpg");
    beta.uploadsPlaylistId = QStringLiteral("UUBETA");
    beta.metadataFetchedAt = fetchedAt.addSecs(1);

    QString error;
    qint64 favoritesId = -1;
    qint64 archiveId = -1;
    {
        Repository repository(sourcePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        favoritesId = repository.addCategory(QStringLiteral("Favorites"), &error);
        archiveId = repository.addCategory(QStringLiteral("Archive"), &error);
        QVERIFY(favoritesId > 0);
        QVERIFY(archiveId > 0);
        QVERIFY2(repository.upsertChannel(alpha, &error), qPrintable(error));
        QVERIFY2(repository.upsertChannel(beta, &error), qPrintable(error));
        QVERIFY(repository.setChannelCategories(alpha.id, {favoritesId, archiveId}, &error));
        QVERIFY(repository.setChannelCategories(beta.id, {archiveId}, &error));
    }

    {
        std::unique_ptr<AppController> controller = AppController::createApplication(sourcePath);
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QVERIFY(controller->exportChannels(QUrl::fromLocalFile(exportPath)));
    }

    QFile file(exportPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(), QStringLiteral("omatube-channels"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    const QJsonArray channels = root.value(QStringLiteral("channels")).toArray();
    QCOMPARE(channels.size(), 2);
    const QJsonObject alphaJson = channels.at(0).toObject();
    QCOMPARE(alphaJson.value(QStringLiteral("id")).toString(), alpha.id);
    QCOMPARE(alphaJson.value(QStringLiteral("originalInput")).toString(), alpha.originalInput);
    QCOMPARE(alphaJson.value(QStringLiteral("handle")).toString(), alpha.handle);
    QCOMPARE(alphaJson.value(QStringLiteral("title")).toString(), alpha.title);
    QCOMPARE(alphaJson.value(QStringLiteral("avatarUrl")).toString(), alpha.avatarUrl);
    QCOMPARE(alphaJson.value(QStringLiteral("uploadsPlaylistId")).toString(), alpha.uploadsPlaylistId);
    QCOMPARE(alphaJson.value(QStringLiteral("metadataFetchedAt")).toString(), fetchedAt.toString(Qt::ISODateWithMs));
    const QVariantList alphaCategories = alphaJson.value(QStringLiteral("categories")).toArray().toVariantList();
    const QVariantList expectedAlphaCategories{QStringLiteral("Favorites"), QStringLiteral("Archive")};
    QCOMPARE(alphaCategories, expectedAlphaCategories);

    {
        Repository repository(targetPath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCExisting"), QStringLiteral("Existing")), &error));
        QVERIFY(repository.addCategory(QStringLiteral("Existing category"), &error) > 0);
    }
    {
        std::unique_ptr<AppController> controller = AppController::createApplication(targetPath);
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QVERIFY(controller->importChannels(QUrl::fromLocalFile(exportPath)));
    }
    Repository imported(targetPath);
    QVERIFY2(imported.open(&error), qPrintable(error));
    QCOMPARE(imported.channels().size(), 3);
    QCOMPARE(imported.channels().at(0), alpha);
    QCOMPARE(imported.channels().at(1), beta);
    const QList<Category> importedCategories = imported.categories();
    QCOMPARE(importedCategories.size(), 3);
    QCOMPARE(importedCategories.at(1).name, QStringLiteral("Favorites"));
    QCOMPARE(importedCategories.at(2).name, QStringLiteral("Archive"));
    const QList<qint64> alphaCategoryIds{importedCategories.at(1).id, importedCategories.at(2).id};
    const QList<qint64> betaCategoryIds{importedCategories.at(2).id};
    QCOMPARE(imported.categoryIdsForChannel(alpha.id), alphaCategoryIds);
    QCOMPARE(imported.categoryIdsForChannel(beta.id), betaCategoryIds);
}

void AppControllerTest::exportsAndImportsCategories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString targetPath = directory.filePath(QStringLiteral("target.sqlite3"));
    const QString exportPath = directory.filePath(QStringLiteral("categories.json"));
    QString error;
    const Channel first = makeChannel(QStringLiteral("UCFirst"), QStringLiteral("First"));
    const Channel second = makeChannel(QStringLiteral("UCSecond"), QStringLiteral("Second"));
    {
        Repository repository(sourcePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(first, &error));
        QVERIFY(repository.upsertChannel(second, &error));
        const qint64 later = repository.addCategory(QStringLiteral("Later"), &error);
        const qint64 earlier = repository.addCategory(QStringLiteral("Earlier"), &error);
        QVERIFY(later > 0 && earlier > 0);
        QVERIFY(repository.moveCategory(earlier, 0, &error));
        QVERIFY(repository.setChannelCategories(first.id, {earlier}, &error));
        QVERIFY(repository.setChannelCategories(second.id, {later}, &error));
    }
    {
        std::unique_ptr<AppController> controller = AppController::createApplication(sourcePath);
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QVERIFY(controller->exportCategories(QUrl::fromLocalFile(exportPath)));
    }
    QFile file(exportPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(), QStringLiteral("omatube-categories"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    const QJsonArray categories = root.value(QStringLiteral("categories")).toArray();
    QCOMPARE(categories.size(), 2);
    QCOMPARE(categories.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Earlier"));
    QCOMPARE(categories.at(0).toObject().value(QStringLiteral("channelIds")).toArray().toVariantList(), QVariantList{first.id});
    QCOMPARE(categories.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Later"));
    QCOMPARE(categories.at(1).toObject().value(QStringLiteral("channelIds")).toArray().toVariantList(), QVariantList{second.id});

    {
        Repository repository(targetPath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(first, &error));
        QVERIFY(repository.upsertChannel(second, &error));
    }
    {
        std::unique_ptr<AppController> controller = AppController::createApplication(targetPath);
        QVERIFY2(controller->initialize(&error), qPrintable(error));
        QVERIFY(controller->importCategories(QUrl::fromLocalFile(exportPath)));
    }
    Repository imported(targetPath);
    QVERIFY2(imported.open(&error), qPrintable(error));
    const QList<Category> result = imported.categories();
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.at(0).name, QStringLiteral("Earlier"));
    QCOMPARE(result.at(1).name, QStringLiteral("Later"));
    QCOMPARE(imported.channels(result.at(0).id).value(0).id, first.id);
    QCOMPARE(imported.channels(result.at(1).id).value(0).id, second.id);
}

void AppControllerTest::rejectsInvalidChannelImportBeforeWrites()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("database.sqlite3"));
    const QString importPath = directory.filePath(QStringLiteral("invalid.json"));
    QString error;
    {
        Repository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCExisting"), QStringLiteral("Existing")), &error));
        QVERIFY(repository.addCategory(QStringLiteral("Existing"), &error) > 0);
    }
    QFile file(importPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject valid{{QStringLiteral("id"), QStringLiteral("UCNew")},
                            {QStringLiteral("title"), QStringLiteral("New")},
                            {QStringLiteral("uploadsPlaylistId"), QStringLiteral("UUNEW")}};
    const QJsonObject invalid{{QStringLiteral("id"), QStringLiteral("UCBad")},
                              {QStringLiteral("title"), 42},
                              {QStringLiteral("uploadsPlaylistId"), QStringLiteral("UUBAD")}};
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("omatube-channels")},
                                        {QStringLiteral("version"), 1},
                                        {QStringLiteral("channels"), QJsonArray{valid, invalid}}})
                   .toJson());
    file.close();

    std::unique_ptr<AppController> controller = AppController::createApplication(databasePath);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    const int channelCount = controller->channels()->rowCount();
    const int categoryCount = controller->categories()->rowCount();
    const QString existingId = controller->channels()->data(
        controller->channels()->index(0), ChannelModel::ChannelIdRole).toString();
    QVERIFY(!controller->importChannels(QUrl::fromLocalFile(importPath)));
    QCOMPARE(controller->channels()->rowCount(), channelCount);
    QCOMPARE(controller->categories()->rowCount(), categoryCount);
    QCOMPARE(controller->channels()->data(controller->channels()->index(0), ChannelModel::ChannelIdRole).toString(), existingId);
    QVERIFY(controller->errorMessage().contains(QStringLiteral("title")));
}

void AppControllerTest::keybindsFooterTextOrdering()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/Keybinds.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QVERIFY2(component.errors().isEmpty(), qPrintable(component.errorString()));

    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QVariant feedVariant;
    const bool feedOk = QMetaObject::invokeMethod(
        object.data(),
        "footerText",
        Q_RETURN_ARG(QVariant, feedVariant),
        Q_ARG(QVariant, QStringLiteral("feed")));
    QVERIFY2(feedOk, "QMetaObject::invokeMethod footerText(\"feed\") failed");
    QCOMPARE(feedVariant.toString(), QStringLiteral("h: history\nw: watch next\ns: config\nj/k: scroll\nr: refresh\nq: quit\nright-click: watch next"));

    QVariant historyVariant;
    const bool historyOk = QMetaObject::invokeMethod(
        object.data(),
        "footerText",
        Q_RETURN_ARG(QVariant, historyVariant),
        Q_ARG(QVariant, QStringLiteral("history")));
    QVERIFY2(historyOk, "QMetaObject::invokeMethod footerText(\"history\") failed");
    QCOMPARE(historyVariant.toString(), QStringLiteral("h: history\nw: watch next\ns: config\nj/k: scroll\nq: quit\nesc: feed\nright-click: delete"));

    QVariant watchNextVariant;
    const bool watchNextOk = QMetaObject::invokeMethod(
        object.data(),
        "footerText",
        Q_RETURN_ARG(QVariant, watchNextVariant),
        Q_ARG(QVariant, QStringLiteral("watchnext")));
    QVERIFY2(watchNextOk, "QMetaObject::invokeMethod footerText(\"watchnext\") failed");
    QCOMPARE(watchNextVariant.toString(), QStringLiteral("h: history\nw: watch next\ns: config\nj/k: scroll\nq: quit\nesc: feed\nright-click: remove"));
}

void AppControllerTest::spaceHoldShortPressEmitsTappedOnly()
{
    SpaceHoldHandler handler;
    QSignalSpy tapped(&handler, &SpaceHoldHandler::tapped);
    QSignalSpy held(&handler, &SpaceHoldHandler::heldChanged);
    QVERIFY(!handler.held());

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &press);
    QTest::qWait(50);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &release);
    QTest::qWait(10);

    QCOMPARE(tapped.count(), 1);
    QCOMPARE(held.count(), 0);
    QVERIFY(!handler.held());
}

void AppControllerTest::spaceHoldLongPressTransitionsHeld()
{
    SpaceHoldHandler handler;
    QSignalSpy tapped(&handler, &SpaceHoldHandler::tapped);
    QSignalSpy held(&handler, &SpaceHoldHandler::heldChanged);
    QVERIFY(!handler.held());

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &press);
    // Cross 200ms threshold.
    QTRY_VERIFY_WITH_TIMEOUT(handler.held(), 800);
    QCOMPARE(held.count(), 1);
    QCOMPARE(tapped.count(), 0);

    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &release);
    QTRY_VERIFY_WITH_TIMEOUT(!handler.held(), 500);
    QCOMPARE(held.count(), 2);
    QCOMPARE(tapped.count(), 0);
}

void AppControllerTest::spaceHoldAutorepeatIgnored()
{
    SpaceHoldHandler handler;
    QSignalSpy tapped(&handler, &SpaceHoldHandler::tapped);
    QSignalSpy held(&handler, &SpaceHoldHandler::heldChanged);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &press);
    // Autorepeat press should be consumed without duplicating.
    QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &repeatPress);
    QKeyEvent repeatRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &repeatRelease);
    QTest::qWait(50);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &release);
    QTest::qWait(10);

    QCOMPARE(tapped.count(), 1);
    QCOMPARE(held.count(), 0);
    QVERIFY(!handler.held());

    // Long hold with autorepeat in middle must still transition held once.
    SpaceHoldHandler handler2;
    QSignalSpy tapped2(&handler2, &SpaceHoldHandler::tapped);
    QSignalSpy held2(&handler2, &SpaceHoldHandler::heldChanged);
    QKeyEvent p2(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &p2);
    QTest::qWait(50);
    QKeyEvent rep2(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &rep2);
    QTRY_VERIFY_WITH_TIMEOUT(handler2.held(), 800);
    QCOMPARE(held2.count(), 1);
    QCOMPARE(tapped2.count(), 0);
    QKeyEvent r2(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &r2);
    QTRY_VERIFY_WITH_TIMEOUT(!handler2.held(), 500);
    QCOMPARE(tapped2.count(), 0);
}

void AppControllerTest::spaceHoldDeactivationClearsHeldWithoutTap()
{
    SpaceHoldHandler handler;
    QSignalSpy tapped(&handler, &SpaceHoldHandler::tapped);
    QSignalSpy held(&handler, &SpaceHoldHandler::heldChanged);

    // Pending press canceled on deactivation without tap.
    {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &press);
        QTest::qWait(30);
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &deactivate);
        QTest::qWait(300);
        QCOMPARE(tapped.count(), 0);
        QCOMPARE(held.count(), 0);
        QVERIFY(!handler.held());
        // Release after deactivation should not emit tapped.
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &release);
        QTest::qWait(10);
        QCOMPARE(tapped.count(), 0);
        QCOMPARE(held.count(), 0);
    }

    // Active hold reset on deactivation.
    {
        SpaceHoldHandler handler2;
        QSignalSpy tapped2(&handler2, &SpaceHoldHandler::tapped);
        QSignalSpy held2(&handler2, &SpaceHoldHandler::heldChanged);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &press);
        QTRY_VERIFY_WITH_TIMEOUT(handler2.held(), 800);
        QCOMPARE(held2.count(), 1);
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &deactivate);
        QTRY_VERIFY_WITH_TIMEOUT(!handler2.held(), 500);
        QCOMPARE(held2.count(), 2);
        QCOMPARE(tapped2.count(), 0);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), false, 1);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &release);
        QTest::qWait(10);
        QCOMPARE(tapped2.count(), 0);
        // No extra held transition.
        QCOMPARE(held2.count(), 2);
    }
}

void AppControllerTest::errorNotificationsCreatesWithSafeDefaults()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QCOMPARE(notifications->property("errorMessage").toString(), QString());
    QCOMPARE(notifications->property("timeoutMs").toInt(), 30000);
    QCOMPARE(notifications->property("maximumVisible").toInt(), 4);
    QCOMPARE(notifications->property("count").toInt(), 0);
    QVERIFY(notifications->property("width").toReal() > 0.0);
    QCOMPARE(notifications->property("height").toReal(), 0.0);
}

void AppControllerTest::errorNotificationsInitialMessageBecomesNotification()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(
        &engine,
        &error,
        QVariantMap{{QStringLiteral("errorMessage"), QStringLiteral("Boom")}});
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QCOMPARE(notifications->property("count").toInt(), 1);

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 1);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Boom"));
}

void AppControllerTest::errorNotificationsIgnoresEmptyMessage()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QString()));
    QCOMPARE(notifications->property("count").toInt(), 0);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Real")));
    QCOMPARE(notifications->property("count").toInt(), 1);

    // An empty errorMessage update is ignored and must not clear anything.
    QVERIFY(notifications->setProperty("errorMessage", QString()));
    QCOMPARE(notifications->property("count").toInt(), 1);
}

void AppControllerTest::errorNotificationsDeduplicatesExactMessages()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Alpha")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Alpha")));
    QCOMPARE(notifications->property("count").toInt(), 1);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Beta")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    // Alpha -> Beta -> Alpha is ignored while Alpha is still visible.
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Alpha")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 2);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Alpha"));
    QCOMPARE(dismissed.at(1).at(0).toString(), QStringLiteral("Beta"));
}

void AppControllerTest::errorNotificationsMaximumVisibleEvictsOldest()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(
        &engine, &error, QVariantMap{{QStringLiteral("maximumVisible"), 2}});
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("One")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Two")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Three")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Four")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    // Oldest entries were evicted without a dismissed signal; survivors are the newest two.
    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 2);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Three"));
    QCOMPARE(dismissed.at(1).at(0).toString(), QStringLiteral("Four"));
}

void AppControllerTest::errorNotificationsDismissRemovesExactlyOne()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("One")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Two")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Three")));
    QCOMPARE(notifications->property("count").toInt(), 3);

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());

    QVERIFY(invokeDismiss(notifications.get(), 1));
    QCOMPARE(notifications->property("count").toInt(), 2);
    QCOMPARE(dismissed.count(), 1);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Two"));

    QVERIFY(invokeDismiss(notifications.get(), 0));
    QVERIFY(invokeDismiss(notifications.get(), 0));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 3);
    QCOMPARE(dismissed.at(1).at(0).toString(), QStringLiteral("One"));
    QCOMPARE(dismissed.at(2).at(0).toString(), QStringLiteral("Three"));
}

void AppControllerTest::errorNotificationsDismissInvalidIndexIsHarmless()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());

    QVERIFY(invokeDismiss(notifications.get(), 0));
    QVERIFY(invokeDismiss(notifications.get(), -1));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 0);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Only")));
    QCOMPARE(notifications->property("count").toInt(), 1);
    QVERIFY(invokeDismiss(notifications.get(), 1));
    QVERIFY(invokeDismiss(notifications.get(), -7));
    QCOMPARE(notifications->property("count").toInt(), 1);
    QCOMPARE(dismissed.count(), 0);
}

void AppControllerTest::errorNotificationsClearRemovesAllWithoutDismissedSignal()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("One")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Two")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());
    QVERIFY(invokeClear(notifications.get()));
    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 0);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Three")));
    QCOMPARE(notifications->property("count").toInt(), 1);
}

void AppControllerTest::errorNotificationsTimeoutAutoDismisses()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(
        &engine, &error, QVariantMap{{QStringLiteral("timeoutMs"), 50}});
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Transient")));
    QCOMPARE(notifications->property("count").toInt(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(notifications->property("count").toInt(), 0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 5000);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Transient"));
}

void AppControllerTest::errorNotificationsHeightGrowsWithDelegates()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    // Standalone (no parent) the root falls back to a usable width.
    QVERIFY(notifications->property("width").toReal() > 0.0);
    QCOMPARE(notifications->property("height").toReal(), 0.0);

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Sized one")));
    QCOMPARE(notifications->property("count").toInt(), 1);

    // The Column computes its implicit size during polish, which requires a
    // window; host the component the way Main.qml does before checking heights.
    QQuickWindow window;
    window.resize(600, 400);
    qobject_cast<QQuickItem *>(notifications.get())->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTRY_VERIFY_WITH_TIMEOUT(notifications->property("height").toReal() > 0.0, 5000);
    const qreal singleHeight = notifications->property("height").toReal();

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Sized two")));
    QCOMPARE(notifications->property("count").toInt(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(notifications->property("height").toReal() > singleHeight, 5000);
}

void AppControllerTest::errorNotificationsLeftClickCopiesMessageAndKeepsCard()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    const QString message = QStringLiteral("Copy this exact text 42");
    QVERIFY(invokePushError(notifications.get(), message));
    QCOMPARE(notifications->property("count").toInt(), 1);

    QQuickWindow window;
    window.resize(600, 400);
    qobject_cast<QQuickItem *>(notifications.get())->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(notifications->property("height").toReal() > 0.0, 5000);

    QQuickItem *rootItem = qobject_cast<QQuickItem *>(notifications.get());
    QVERIFY(rootItem != nullptr);
    const QList<QQuickItem *> cards =
        findVisualChildrenByName(rootItem, QStringLiteral("errorNotificationCard"));
    QCOMPARE(cards.size(), 1);
    QQuickItem *card = cards.first();

    // Sentinel so the copy must have replaced the clipboard content.
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));

    const QPoint center =
        card->mapToScene(QPointF(card->width() / 2.0, card->height() / 2.0)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);

    QCOMPARE(QGuiApplication::clipboard()->text(), message);
    QCOMPARE(notifications->property("count").toInt(), 1);
}

void AppControllerTest::errorNotificationsRightClickDismissesCard()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("RightBody")));
    QCOMPARE(notifications->property("count").toInt(), 1);

    QQuickWindow window;
    window.resize(600, 400);
    qobject_cast<QQuickItem *>(notifications.get())->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(notifications->property("height").toReal() > 0.0, 5000);

    QQuickItem *rootItem = qobject_cast<QQuickItem *>(notifications.get());
    QVERIFY(rootItem != nullptr);
    const QList<QQuickItem *> cards =
        findVisualChildrenByName(rootItem, QStringLiteral("errorNotificationCard"));
    QCOMPARE(cards.size(), 1);
    QQuickItem *card = cards.first();

    QGuiApplication::clipboard()->clear();
    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());

    const QPoint center =
        card->mapToScene(QPointF(card->width() / 2.0, card->height() / 2.0)).toPoint();
    QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier, center);

    QCOMPARE(notifications->property("count").toInt(), 0);
    QCOMPARE(dismissed.count(), 1);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("RightBody"));
    // Right-click dismisses; it must not copy.
    QCOMPARE(QGuiApplication::clipboard()->text(), QString());
}

void AppControllerTest::errorNotificationsCloseButtonDismissesCard()
{
    QQmlEngine engine;
    QString error;
    std::unique_ptr<QObject> notifications = createErrorNotifications(&engine, &error);
    QVERIFY2(notifications != nullptr, qPrintable(error));

    QVERIFY(invokePushError(notifications.get(), QStringLiteral("First")));
    QVERIFY(invokePushError(notifications.get(), QStringLiteral("Second")));
    QCOMPARE(notifications->property("count").toInt(), 2);

    QQuickWindow window;
    window.resize(600, 400);
    qobject_cast<QQuickItem *>(notifications.get())->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(notifications->property("height").toReal() > 0.0, 5000);

    QQuickItem *rootItem = qobject_cast<QQuickItem *>(notifications.get());
    QVERIFY(rootItem != nullptr);
    const QList<QQuickItem *> cards =
        findVisualChildrenByName(rootItem, QStringLiteral("errorNotificationCard"));
    QCOMPARE(cards.size(), 2);
    QQuickItem *secondCard = cards.last();
    QCOMPARE(secondCard->property("message").toString(), QStringLiteral("Second"));
    QQuickItem *closeArea =
        secondCard->findChild<QQuickItem *>(QStringLiteral("errorNotificationClose"));
    QVERIFY(closeArea != nullptr);

    QGuiApplication::clipboard()->clear();
    QSignalSpy dismissed(notifications.get(), SIGNAL(dismissed(QString)));
    QVERIFY(dismissed.isValid());

    const QPoint center =
        closeArea->mapToScene(QPointF(closeArea->width() / 2.0, closeArea->height() / 2.0)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);

    QCOMPARE(notifications->property("count").toInt(), 1);
    QCOMPARE(dismissed.count(), 1);
    QCOMPARE(dismissed.at(0).at(0).toString(), QStringLiteral("Second"));
    // Close clicks are consumed by the close area, not the body copy area.
    QCOMPARE(QGuiApplication::clipboard()->text(), QString());
}

QTEST_MAIN(AppControllerTest)

#include "appcontroller_test.moc"
