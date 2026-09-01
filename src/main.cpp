#include "appcontroller.h"
#include "spaceholdhandler.h"

#ifdef OMA_HAS_MPV
#include "mpvplayer.h"
#endif

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QPointer>
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
    qmlRegisterType<SpaceHoldHandler>("YtClient", 1, 0, "SpaceHoldHandler");
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
    const QUrl initialUrl = QUrl(controller->simpleUi()
        ? QStringLiteral("qrc:/qml/SimpleMain.qml")
        : QStringLiteral("qrc:/qml/Main.qml"));
    engine.load(initialUrl);
    QPointer<QQuickWindow> currentWindow =
        qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));

    // The QML toggle handler must finish before the root object is replaced,
    // so the swap is deferred to the next event loop turn. The replacement is
    // created and shown before the old root is discarded to avoid triggering
    // quit-on-last-window while no window exists.
    QObject::connect(
        controller.get(),
        &AppController::simpleUiChanged,
        &engine,
        [&engine, &currentWindow, controller = controller.get()]() {
            QTimer::singleShot(0, &engine, [&engine, &currentWindow, controller]() {
                QQuickWindow *oldWindow = currentWindow;
                if (!oldWindow)
                    return;

                engine.load(QUrl(controller->simpleUi()
                    ? QStringLiteral("qrc:/qml/SimpleMain.qml")
                    : QStringLiteral("qrc:/qml/Main.qml")));
                QQuickWindow *newWindow =
                    qobject_cast<QQuickWindow *>(engine.rootObjects().constLast());
                if (!newWindow || newWindow == oldWindow)
                    return;

                newWindow->setScreen(oldWindow->screen());
                newWindow->setGeometry(oldWindow->geometry());
                newWindow->setVisibility(oldWindow->visibility());
                currentWindow = newWindow;
                oldWindow->deleteLater();
            });
        });

    if (smokeTest)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);

    return app.exec();
}
