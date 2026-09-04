#include "appcontroller.h"
#include "automationfixture.h"
#include "spaceholdhandler.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqml.h>

class NavigationTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void fullUiNavigation();
    void simpleUiNavigation();

private:
    QTemporaryDir m_settingsDirectory;
};

namespace {
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

QQuickItem *firstVisualChild(QQuickItem *parent, const QString &name)
{
    const QList<QQuickItem *> matches = findVisualChildrenByName(parent, name);
    return matches.isEmpty() ? nullptr : matches.constFirst();
}

QQuickWindow *findWindowByName(QQmlApplicationEngine *engine, const QString &name)
{
    for (QObject *root : engine->rootObjects()) {
        if (root->objectName() == name)
            return qobject_cast<QQuickWindow *>(root);
        if (QQuickWindow *child = root->findChild<QQuickWindow *>(name))
            return child;
    }
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        if (window->objectName() == name)
            return qobject_cast<QQuickWindow *>(window);
    }
    return nullptr;
}

// Translate the item center into target window coordinates so QtTest clicks
// hit the same control a user would tap, including JS-owned delegates.
void clickItem(QQuickItem *item)
{
    QVERIFY(item != nullptr);
    QQuickWindow *window = item->window();
    QVERIFY(window != nullptr);
    const QPointF sceneCenter =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPoint target = window->contentItem()->mapFromScene(sceneCenter).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target);
}

const QStringList settingsTabNames{
    QStringLiteral("settingsChannelsTab"),
    QStringLiteral("settingsCategoriesTab"),
    QStringLiteral("settingsFeedTab"),
    QStringLiteral("settingsAppearanceTab"),
    QStringLiteral("settingsApiTab"),
    QStringLiteral("settingsPlaybackTab"),
};
} // namespace

void NavigationTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("OmaTubeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("navigation_tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        m_settingsDirectory.path());
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Registered once, exactly as the application does; the factory always
    // resolves to the single live AppController instance.
    qmlRegisterSingletonType<AppController>("YtClient", 1, 0, "App", &AppController::create);
    qmlRegisterType<SpaceHoldHandler>("YtClient", 1, 0, "SpaceHoldHandler");
}

void NavigationTest::init()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void NavigationTest::fullUiNavigation()
{
    QTemporaryDir databaseDirectory;
    QVERIFY(databaseDirectory.isValid());
    const QString databasePath = databaseDirectory.filePath(QStringLiteral("navigation.sqlite3"));
    QString seedError;
    QVERIFY2(AutomationFixture::seed(databasePath, &seedError), qPrintable(seedError));

    std::unique_ptr<AppController> controller =
        AppController::createApplication(databasePath, true);
    QString initializeError;
    QVERIFY2(controller->initialize(&initializeError), qPrintable(initializeError));
    QVERIFY(controller->automationMode());

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QTRY_VERIFY(!engine.rootObjects().isEmpty());
    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window != nullptr);
    QCOMPARE(window->objectName(), QStringLiteral("appWindow"));
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    QQuickItem *rootItem = window->contentItem();
    QVERIFY(rootItem != nullptr);

    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("feedPage")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedNavigationButton")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("historyNavigationButton")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("watchNextNavigationButton")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("settingsNavigationButton")).isEmpty());
    QQuickItem *refreshButton = nullptr;
    QTRY_VERIFY((refreshButton = firstVisualChild(rootItem, QStringLiteral("refreshButton")))
                != nullptr);
    QVERIFY(!refreshButton->isEnabled());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("historyLoader")).isEmpty());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("playerLoader")).isEmpty());

    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("categoryButton_1")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("categoryButton_2")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000001")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000005")).isEmpty());

    QQuickItem *feedVideo = firstVisualChild(rootItem, QStringLiteral("feedVideo_AUTO0000001"));
    QVERIFY(feedVideo != nullptr);
    QTRY_VERIFY(feedVideo->width() > 0.0);
    const qreal feedCardWidth = feedVideo->width();
    clickItem(feedVideo);
    QTRY_VERIFY(controller->playerOpen());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("automationPlayer")).isEmpty());
    QQuickItem *feedBackButton = nullptr;
    QTRY_VERIFY((feedBackButton = firstVisualChild(
                     rootItem,
                     QStringLiteral("playerBackButton")))
                != nullptr);
    clickItem(feedBackButton);
    QTRY_VERIFY(!controller->playerOpen());

    QQuickItem *techCategory = firstVisualChild(rootItem, QStringLiteral("categoryButton_2"));
    QVERIFY(techCategory != nullptr);
    QTRY_VERIFY(techCategory->isVisible());
    clickItem(techCategory);
    QTRY_VERIFY(controller->selectedCategoryId() == 2);
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000004")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000005")).isEmpty());
    for (int index = 1; index <= 3; ++index) {
        QTRY_VERIFY(findVisualChildrenByName(
                        rootItem,
                        QStringLiteral("feedVideo_AUTO000000%1").arg(index))
                        .isEmpty());
    }

    QQuickItem *historyNavigation =
        firstVisualChild(rootItem, QStringLiteral("historyNavigationButton"));
    QVERIFY(historyNavigation != nullptr);
    clickItem(historyNavigation);
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("historyPage")).isEmpty());
    QQuickItem *historyPage = firstVisualChild(rootItem, QStringLiteral("historyPage"));
    QVERIFY(historyPage != nullptr);
    QTRY_VERIFY(!findVisualChildrenByName(historyPage, QStringLiteral("historyVideo_AUTO0000001"))
                     .isEmpty());

    QQuickItem *watchNextNavigation =
        firstVisualChild(rootItem, QStringLiteral("watchNextNavigationButton"));
    QVERIFY(watchNextNavigation != nullptr);
    clickItem(watchNextNavigation);
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("watchNextPage")).isEmpty());
    QQuickItem *watchNextPage = firstVisualChild(rootItem, QStringLiteral("watchNextPage"));
    QVERIFY(watchNextPage != nullptr);
    QTRY_VERIFY(!findVisualChildrenByName(watchNextPage, QStringLiteral("watchNextVideo_AUTO0000002"))
                     .isEmpty());
    QTRY_VERIFY(!findVisualChildrenByName(watchNextPage, QStringLiteral("watchNextVideo_AUTO0000004"))
                     .isEmpty());
    QQuickItem *queueFirst = nullptr;
    QTRY_VERIFY((queueFirst = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideo_AUTO0000002")))
                != nullptr);
    QQuickItem *queueSecond = nullptr;
    QTRY_VERIFY((queueSecond = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideo_AUTO0000004")))
                != nullptr);
    QTRY_VERIFY(queueFirst->width() > 0.0);
    QTRY_VERIFY(queueSecond->width() > 0.0);
    QTRY_VERIFY(qAbs(queueFirst->width() - feedCardWidth) <= 0.5);
    QTRY_VERIFY(qAbs(queueSecond->width() - feedCardWidth) <= 0.5);
    QTRY_VERIFY(
        qAbs((queueSecond->x() - queueFirst->x()) - (queueFirst->width() + 12.0)) <= 0.5);

    QQuickItem *feedNavigation =
        firstVisualChild(rootItem, QStringLiteral("feedNavigationButton"));
    QVERIFY(feedNavigation != nullptr);
    clickItem(feedNavigation);
    QTRY_VERIFY(findVisualChildrenByName(rootItem, QStringLiteral("watchNextPage")).isEmpty());
    // The category filter from the earlier step is still active; clear it
    // before expecting the full feed again.
    QQuickItem *allCategories = firstVisualChild(rootItem, QStringLiteral("categoryButton_2"));
    QVERIFY(allCategories != nullptr);
    clickItem(allCategories);
    QTRY_VERIFY(controller->selectedCategoryId() == -1);
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000001")).isEmpty());

    QQuickItem *settingsNavigation =
        firstVisualChild(rootItem, QStringLiteral("settingsNavigationButton"));
    QVERIFY(settingsNavigation != nullptr);
    clickItem(settingsNavigation);
    QQuickWindow *settingsWindow = nullptr;
    QTRY_VERIFY((settingsWindow = findWindowByName(&engine, QStringLiteral("settingsWindow")))
                != nullptr);
    QVERIFY(QTest::qWaitForWindowExposed(settingsWindow));
    QQuickItem *settingsRoot = settingsWindow->contentItem();
    QVERIFY(settingsRoot != nullptr);

    QQuickItem *settingsTabs = nullptr;
    QTRY_VERIFY((settingsTabs = firstVisualChild(settingsRoot, QStringLiteral("settingsTabs")))
                != nullptr);
    for (const QString &tabName : settingsTabNames)
        QTRY_VERIFY(!findVisualChildrenByName(settingsRoot, tabName).isEmpty());
    for (int index = 0; index < settingsTabNames.size(); ++index) {
        QQuickItem *tab = firstVisualChild(settingsRoot, settingsTabNames.at(index));
        QVERIFY(tab != nullptr);
        QTRY_VERIFY(tab->isVisible());
        clickItem(tab);
        QTRY_VERIFY(settingsTabs->property("currentIndex").toInt() == index);
    }

    QQuickItem *closeButton =
        firstVisualChild(settingsRoot, QStringLiteral("settingsCloseButton"));
    QVERIFY(closeButton != nullptr);
    QTRY_VERIFY(closeButton->isVisible());
    clickItem(closeButton);
    QTRY_VERIFY(!settingsWindow->isVisible());

    QVERIFY(!controller->refreshing());
    QVERIFY(controller->automationMode());
}

void NavigationTest::simpleUiNavigation()
{
    QSettings().setValue(QStringLiteral("appearance/simpleUi"), true);
    QSettings().sync();

    QTemporaryDir databaseDirectory;
    QVERIFY(databaseDirectory.isValid());
    const QString databasePath = databaseDirectory.filePath(QStringLiteral("navigation.sqlite3"));
    QString seedError;
    QVERIFY2(AutomationFixture::seed(databasePath, &seedError), qPrintable(seedError));

    std::unique_ptr<AppController> controller =
        AppController::createApplication(databasePath, true);
    QString initializeError;
    QVERIFY2(controller->initialize(&initializeError), qPrintable(initializeError));
    QVERIFY(controller->automationMode());
    QVERIFY(controller->simpleUi());

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/SimpleMain.qml")));
    QTRY_VERIFY(!engine.rootObjects().isEmpty());
    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window != nullptr);
    QCOMPARE(window->objectName(), QStringLiteral("appWindow"));
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QTRY_VERIFY(QGuiApplication::focusWindow() == window);

    const QString statusBeforeRefreshKey = controller->statusMessage();
    QTest::keyClick(window, Qt::Key_R);
    QTRY_COMPARE(controller->statusMessage(), statusBeforeRefreshKey);

    QQuickItem *rootItem = window->contentItem();
    QVERIFY(rootItem != nullptr);

    QVERIFY(!controller->refreshing());
    QVERIFY(!controller->historyLoading());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("feedPage")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000001")).isEmpty());

    QTest::keyClick(window, Qt::Key_H);
    QTRY_VERIFY(window->property("historyOpen").toBool());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("historyLoader")).isEmpty());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("historyPage")).isEmpty());
    QQuickItem *historyPage = firstVisualChild(rootItem, QStringLiteral("historyPage"));
    QVERIFY(historyPage != nullptr);
    QQuickItem *historyRow = nullptr;
    QTRY_VERIFY((historyRow = firstVisualChild(
                     historyPage,
                     QStringLiteral("historyVideo_AUTO0000001")))
                != nullptr);
    clickItem(historyRow);
    QTRY_VERIFY(controller->playerOpen());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("playerPage")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("automationPlayer")).isEmpty());

    QQuickItem *backButton = nullptr;
    QTRY_VERIFY((backButton = firstVisualChild(rootItem, QStringLiteral("playerBackButton")))
                != nullptr);
    clickItem(backButton);
    QTRY_VERIFY(!controller->playerOpen());

    QTest::keyClick(window, Qt::Key_W);
    QTRY_VERIFY(window->property("watchNextOpen").toBool());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("watchNextLoader")).isEmpty());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("watchNextPage")).isEmpty());
    QQuickItem *watchNextPage = firstVisualChild(rootItem, QStringLiteral("watchNextPage"));
    QVERIFY(watchNextPage != nullptr);
    QQuickItem *queueRow = nullptr;
    QTRY_VERIFY((queueRow = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideo_AUTO0000002")))
                != nullptr);
    QQuickItem *queueSecond = nullptr;
    QTRY_VERIFY((queueSecond = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideo_AUTO0000004")))
                != nullptr);
    QTRY_VERIFY(queueRow->width() > 0.0);
    QTRY_VERIFY(queueSecond->width() > 0.0);
    QTRY_VERIFY(watchNextPage->width() > 0.0);
    QTRY_VERIFY(qAbs((queueSecond->x() - queueRow->x()) - (queueRow->width() + 12.0)) <= 0.5);
    QVERIFY(queueRow->width() < watchNextPage->width() / 2.0);
    clickItem(queueRow);
    QTRY_VERIFY(controller->playerOpen());
    QTRY_VERIFY(!findVisualChildrenByName(rootItem, QStringLiteral("playerPage")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("automationPlayer")).isEmpty());

    QQuickItem *queueBackButton = nullptr;
    QTRY_VERIFY((queueBackButton = firstVisualChild(rootItem, QStringLiteral("playerBackButton")))
                != nullptr);
    clickItem(queueBackButton);
    QTRY_VERIFY(!controller->playerOpen());

    QTest::keyClick(window, Qt::Key_S);
    QQuickWindow *settingsWindow = nullptr;
    QTRY_VERIFY((settingsWindow = findWindowByName(&engine, QStringLiteral("settingsWindow")))
                != nullptr);
    QTRY_VERIFY(settingsWindow->isVisible());
    QQuickItem *settingsRoot = settingsWindow->contentItem();
    QVERIFY(settingsRoot != nullptr);
    for (const QString &tabName : settingsTabNames)
        QTRY_VERIFY(!findVisualChildrenByName(settingsRoot, tabName).isEmpty());
    QQuickItem *settingsTabs = firstVisualChild(settingsRoot, QStringLiteral("settingsTabs"));
    QVERIFY(settingsTabs != nullptr);
    QQuickItem *lastTab =
        firstVisualChild(settingsRoot, QStringLiteral("settingsPlaybackTab"));
    QVERIFY(lastTab != nullptr);
    clickItem(lastTab);
    QTRY_VERIFY(settingsTabs->property("currentIndex").toInt() == 5);

    QTest::keyClick(settingsWindow, Qt::Key_Escape);
    if (settingsWindow->isVisible()) {
        QQuickItem *closeButton =
            firstVisualChild(settingsRoot, QStringLiteral("settingsCloseButton"));
        QVERIFY(closeButton != nullptr);
        QTRY_VERIFY(closeButton->isVisible());
        clickItem(closeButton);
    }
    QTRY_VERIFY(!settingsWindow->isVisible());

    QVERIFY(!controller->refreshing());
    QVERIFY(!controller->historyLoading());
    QVERIFY(controller->automationMode());
}

QTEST_MAIN(NavigationTest)

#include "navigation_test.moc"
