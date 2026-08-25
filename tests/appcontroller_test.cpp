#include "appcontroller.h"

#include <QSignalSpy>
#include <QStandardPaths>
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
};

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

QTEST_GUILESS_MAIN(AppControllerTest)

#include "appcontroller_test.moc"
