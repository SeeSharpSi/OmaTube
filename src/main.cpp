#include "appcontroller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>

int main(int argc, char *argv[])
{
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
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("YtClient"), QStringLiteral("Main"));

    if (smokeTest)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);

    return app.exec();
}
