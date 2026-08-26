#include "appcontroller.h"

#ifdef OMA_HAS_MPV
#include "mpvplayer.h"
#endif

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QSGRendererInterface>
#include <QtQml/qqml.h>

#include <clocale>

#ifdef Q_OS_LINUX
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif

#ifdef Q_OS_MACOS
#include "macvideoplayer.h"
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    QCoreApplication::setOrganizationName(QStringLiteral("YT Client"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ytclient.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("YT Client"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const bool smokeTest = app.arguments().contains(QStringLiteral("--quit-after-startup"));
#ifdef OMA_HAS_MPV
    if (!smokeTest)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    std::unique_ptr<AppController> controller = AppController::createApplication(
        smokeTest ? QStringLiteral(":memory:") : QString{});
    QString initializationError;
    if (!controller->initialize(&initializationError)) {
        qCritical("Could not initialize application: %s", qPrintable(initializationError));
        return EXIT_FAILURE;
    }
    qmlRegisterSingletonType<AppController>(
        "YtClient", 1, 0, "App", &AppController::create);
#ifdef Q_OS_MACOS
    qmlRegisterType<MacVideoPlayerNative>("YtClient", 1, 0, "MacVideoPlayerNative");
#endif
#ifdef OMA_HAS_MPV
    qmlRegisterType<MpvPlayerNative>("YtClient", 1, 0, "MpvPlayerNative");
#endif
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (smokeTest)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);

    return app.exec();
}
