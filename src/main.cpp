#include "appcontroller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqml.h>

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
    QCoreApplication::setOrganizationName(QStringLiteral("YT Client"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ytclient.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("YT Client"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const bool smokeTest = app.arguments().contains(QStringLiteral("--quit-after-startup"));
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
