#include "appcontroller.h"
#include "automationfixture.h"
#include "spaceholdhandler.h"

#include <QCoreApplication>
#include <QColor>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
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
    void exclusiveRoutes_data();
    void exclusiveRoutes();

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

// Same coordinate mapping as clickItem, but only moves the pointer so
// HoverHandler hovered state can be asserted deterministically offscreen.
void moveMouseToItem(QQuickItem *item)
{
    QVERIFY(item != nullptr);
    QQuickWindow *window = item->window();
    QVERIFY(window != nullptr);
    const QPointF sceneCenter =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPoint target = window->contentItem()->mapFromScene(sceneCenter).toPoint();
    QTest::mouseMove(window, target);
}

// Move to an explicit local point for cards where the hover area is only
// the upper clickable region (full Watch Next queue controls sit below it).
void moveMouseToItemPoint(QQuickItem *item, const QPointF &local)
{
    QVERIFY(item != nullptr);
    QQuickWindow *window = item->window();
    QVERIFY(window != nullptr);
    const QPointF scenePoint = item->mapToScene(local);
    const QPoint target = window->contentItem()->mapFromScene(scenePoint).toPoint();
    QTest::mouseMove(window, target);
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
    controller->liveChannels()->setLiveChannels(
        {LiveChannel{QStringLiteral("UCAlpha"),
                     QStringLiteral("Live Channel"),
                     {},
                     QStringLiteral("LIVEAUTO001"),
                     QStringLiteral("Automation Live Stream")}});

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
    QQuickItem *feedbackNotice = nullptr;
    QTRY_VERIFY((feedbackNotice = firstVisualChild(rootItem, QStringLiteral("feedbackNotice")))
                != nullptr);
    {
        const QColor borderColor =
            QQmlProperty::read(feedbackNotice, QStringLiteral("border.color")).value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
    QVERIFY(!feedbackNotice->isVisible());
    QQuickItem *feedbackLabel = nullptr;
    QTRY_VERIFY((feedbackLabel = firstVisualChild(rootItem, QStringLiteral("feedbackLabel")))
                != nullptr);
    QVERIFY(controller->addToWatchNext(QStringLiteral("AUTO0000001")));
    QTRY_VERIFY(feedbackNotice->isVisible());
    QTRY_COMPARE(
        feedbackLabel->property("text").toString(),
        QStringLiteral("Added to Watch Next"));

    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("categoryButton_1")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("categoryButton_2")).isEmpty());
    QQuickItem *categoryButton1 =
        firstVisualChild(rootItem, QStringLiteral("categoryButton_1"));
    QVERIFY(categoryButton1 != nullptr);
    QTRY_VERIFY(categoryButton1->isVisible());
    QVERIFY(categoryButton1->width() > 0.0);
    QVERIFY(categoryButton1->height() > 0.0);
    QQuickItem *categoryOutline1 = nullptr;
    QTRY_VERIFY((categoryOutline1 = firstVisualChild(
                     rootItem,
                     QStringLiteral("categoryButtonOutline_1")))
                != nullptr);
    QCOMPARE(controller->selectedCategoryId(), qint64(-1));
    {
        const QColor borderColor = QQmlProperty::read(
                                       categoryOutline1, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor ruleColor = window->property("rule").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), ruleColor.red());
        QCOMPARE(borderColor.green(), ruleColor.green());
        QCOMPARE(borderColor.blue(), ruleColor.blue());
    }
    moveMouseToItem(categoryButton1);
    QTRY_VERIFY(categoryButton1->property("hovered").toBool());
    {
        const QColor borderColor = QQmlProperty::read(
                                       categoryOutline1, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000001")).isEmpty());
    QTRY_VERIFY(
        !findVisualChildrenByName(rootItem, QStringLiteral("feedVideo_AUTO0000005")).isEmpty());

    QQuickItem *liveNowLabel = nullptr;
    QTRY_VERIFY((liveNowLabel = firstVisualChild(rootItem, QStringLiteral("liveNowLabel")))
                != nullptr);
    QTRY_COMPARE(liveNowLabel->property("text").toString(), QStringLiteral("LIVE NOW"));
    QQuickItem *liveDelegate = nullptr;
    QTRY_VERIFY((liveDelegate =
                     firstVisualChild(rootItem, QStringLiteral("liveVideo_LIVEAUTO001")))
                != nullptr);
    QTRY_VERIFY(liveDelegate->width() > 0.0);
    moveMouseToItem(liveDelegate);
    QTRY_COMPARE(liveNowLabel->property("text").toString(),
                 QStringLiteral("Automation Live Stream"));
    QQuickItem *nonLiveFeedVideo =
        firstVisualChild(rootItem, QStringLiteral("feedVideo_AUTO0000001"));
    QTRY_VERIFY(nonLiveFeedVideo != nullptr);
    moveMouseToItem(nonLiveFeedVideo);
    QTRY_COMPARE(liveNowLabel->property("text").toString(), QStringLiteral("LIVE NOW"));

    QQuickItem *feedVideo = firstVisualChild(rootItem, QStringLiteral("feedVideo_AUTO0000001"));
    QVERIFY(feedVideo != nullptr);
    QTRY_VERIFY(feedVideo->width() > 0.0);
    const qreal feedCardWidth = feedVideo->width();
    QQuickItem *feedOutline = nullptr;
    QTRY_VERIFY((feedOutline = firstVisualChild(
                     rootItem,
                     QStringLiteral("feedVideoOutline_AUTO0000001")))
                != nullptr);
    QVERIFY(feedOutline->z() > 0.0);
    moveMouseToItem(feedVideo);
    QTRY_COMPARE(
        QQmlProperty::read(feedOutline, QStringLiteral("border.width")).toInt(), 2);
    {
        const QColor borderColor =
            QQmlProperty::read(feedOutline, QStringLiteral("border.color")).value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
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
    QQuickItem *historyCard = firstVisualChild(
        historyPage, QStringLiteral("historyVideo_AUTO0000001"));
    QVERIFY(historyCard != nullptr);
    QTRY_VERIFY(historyCard->width() > 0.0);
    QQuickItem *historyOutline = nullptr;
    QTRY_VERIFY((historyOutline = firstVisualChild(
                     historyPage,
                     QStringLiteral("historyVideoOutline_AUTO0000001")))
                != nullptr);
    QVERIFY(historyOutline->z() > 0.0);
    moveMouseToItem(historyCard);
    QTRY_COMPARE(
        QQmlProperty::read(historyOutline, QStringLiteral("border.width")).toInt(), 2);
    {
        const QColor borderColor =
            QQmlProperty::read(historyOutline, QStringLiteral("border.color")).value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }

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
    QQuickItem *queueOutline = nullptr;
    QTRY_VERIFY((queueOutline = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideoOutline_AUTO0000002")))
                != nullptr);
    QVERIFY(queueOutline->z() > 0.0);
    QTRY_VERIFY(queueOutline->height() > 10.0);
    moveMouseToItemPoint(
        queueOutline, QPointF(queueOutline->width() / 2.0, 10.0));
    QTRY_COMPARE(
        QQmlProperty::read(queueOutline, QStringLiteral("border.width")).toInt(), 2);
    {
        const QColor borderColor =
            QQmlProperty::read(queueOutline, QStringLiteral("border.color")).value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }

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
    controller->liveChannels()->setLiveChannels(
        {LiveChannel{QStringLiteral("UCAlpha"),
                     QStringLiteral("Live Channel"),
                     {},
                     QStringLiteral("LIVEAUTO001"),
                     QStringLiteral("Automation Live Stream")}});

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

    QQuickItem *simpleLiveNowLabel = nullptr;
    QTRY_VERIFY((simpleLiveNowLabel = firstVisualChild(rootItem, QStringLiteral("liveNowLabel")))
                != nullptr);
    QTRY_COMPARE(simpleLiveNowLabel->property("text").toString(), QStringLiteral("LIVE NOW"));
    QQuickItem *simpleLiveDelegate = nullptr;
    QTRY_VERIFY((simpleLiveDelegate =
                     firstVisualChild(rootItem, QStringLiteral("liveVideo_LIVEAUTO001")))
                != nullptr);
    QTRY_VERIFY(simpleLiveDelegate->width() > 0.0);
    moveMouseToItem(simpleLiveDelegate);
    QTRY_COMPARE(simpleLiveNowLabel->property("text").toString(),
                 QStringLiteral("Automation Live Stream"));
    QQuickItem *simpleNonLiveFeed =
        firstVisualChild(rootItem, QStringLiteral("feedVideo_AUTO0000001"));
    QTRY_VERIFY(simpleNonLiveFeed != nullptr);
    moveMouseToItem(simpleNonLiveFeed);
    QTRY_COMPARE(simpleLiveNowLabel->property("text").toString(), QStringLiteral("LIVE NOW"));

    const int unselectedCategoryId = controller->selectedCategoryId() == 1 ? 2 : 1;
    QQuickItem *simpleCategoryButton = firstVisualChild(
        rootItem, QStringLiteral("categoryButton_%1").arg(unselectedCategoryId));
    QVERIFY(simpleCategoryButton != nullptr);
    QTRY_VERIFY(simpleCategoryButton->isVisible());
    QVERIFY(simpleCategoryButton->width() > 0.0);
    QVERIFY(simpleCategoryButton->height() > 0.0);
    QQuickItem *simpleCategoryOutline = nullptr;
    QTRY_VERIFY((simpleCategoryOutline = firstVisualChild(
                     rootItem,
                     QStringLiteral("categoryButtonOutline_%1").arg(unselectedCategoryId)))
                != nullptr);
    {
        const QColor borderColor = QQmlProperty::read(
                                       simpleCategoryOutline, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor ruleColor = window->property("rule").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), ruleColor.red());
        QCOMPARE(borderColor.green(), ruleColor.green());
        QCOMPARE(borderColor.blue(), ruleColor.blue());
    }
    moveMouseToItem(simpleCategoryButton);
    QTRY_VERIFY(simpleCategoryButton->property("hovered").toBool());
    {
        const QColor borderColor = QQmlProperty::read(
                                       simpleCategoryOutline, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
    QQuickItem *simpleFeedVideo =
        firstVisualChild(rootItem, QStringLiteral("feedVideo_AUTO0000001"));
    QVERIFY(simpleFeedVideo != nullptr);
    QTRY_VERIFY(simpleFeedVideo->width() > 0.0);
    QQuickItem *simpleFeedOutline = nullptr;
    QTRY_VERIFY((simpleFeedOutline = firstVisualChild(
                     rootItem,
                     QStringLiteral("feedVideoOutline_AUTO0000001")))
                != nullptr);
    QVERIFY(simpleFeedOutline->z() > 0.0);
    QTRY_COMPARE(
        QQmlProperty::read(simpleFeedOutline, QStringLiteral("border.width")).toInt(), 0);
    moveMouseToItem(simpleFeedVideo);
    QTRY_COMPARE(
        QQmlProperty::read(simpleFeedOutline, QStringLiteral("border.width")).toInt(), 2);
    {
        const QColor borderColor =
            QQmlProperty::read(simpleFeedOutline, QStringLiteral("border.color")).value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
    QQuickItem *simpleFeedbackNotice = nullptr;
    QTRY_VERIFY((simpleFeedbackNotice = firstVisualChild(rootItem, QStringLiteral("feedbackNotice")))
                != nullptr);
    {
        const QColor borderColor = QQmlProperty::read(
                                       simpleFeedbackNotice, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
    QVERIFY(!simpleFeedbackNotice->isVisible());
    QQuickItem *simpleFeedbackLabel = nullptr;
    QTRY_VERIFY((simpleFeedbackLabel = firstVisualChild(rootItem, QStringLiteral("feedbackLabel")))
                != nullptr);
    QVERIFY(controller->addToWatchNext(QStringLiteral("AUTO0000002")));
    QTRY_VERIFY(simpleFeedbackNotice->isVisible());
    QTRY_COMPARE(
        simpleFeedbackLabel->property("text").toString(),
        QStringLiteral("Already in Watch Next"));

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
    QTRY_VERIFY(queueRow->height() >= queueRow->width() * 0.5625);
    QTRY_VERIFY(queueSecond->height() >= queueSecond->width() * 0.5625);
    QTRY_VERIFY(qAbs((queueSecond->x() - queueRow->x()) - (queueRow->width() + 12.0)) <= 0.5);
    QVERIFY(queueRow->width() < watchNextPage->width() / 2.0);
    QQuickItem *simpleQueueOutline = nullptr;
    QTRY_VERIFY((simpleQueueOutline = firstVisualChild(
                     watchNextPage,
                     QStringLiteral("watchNextVideoOutline_AUTO0000002")))
                != nullptr);
    QVERIFY(simpleQueueOutline->z() > 0.0);
    moveMouseToItem(queueRow);
    QTRY_COMPARE(
        QQmlProperty::read(simpleQueueOutline, QStringLiteral("border.width")).toInt(), 2);
    {
        const QColor borderColor = QQmlProperty::read(
                                       simpleQueueOutline, QStringLiteral("border.color"))
                                       .value<QColor>();
        const QColor accentColor = window->property("accent").value<QColor>();
        QCOMPARE(borderColor.alpha(), 255);
        QCOMPARE(borderColor.red(), accentColor.red());
        QCOMPARE(borderColor.green(), accentColor.green());
        QCOMPARE(borderColor.blue(), accentColor.blue());
    }
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

void NavigationTest::exclusiveRoutes_data()
{
    QTest::addColumn<bool>("simpleUi");
    QTest::newRow("fullUi") << false;
    QTest::newRow("simpleUi") << true;
}

void NavigationTest::exclusiveRoutes()
{
    QFETCH(bool, simpleUi);
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/simpleUi"), simpleUi);
    settings.sync();
    QTemporaryDir databaseDirectory;
    QVERIFY(databaseDirectory.isValid());
    const QString databasePath = databaseDirectory.filePath(QStringLiteral("navigation.sqlite3"));
    QString error;
    QVERIFY2(AutomationFixture::seed(databasePath, &error), qPrintable(error));
    auto controller = AppController::createApplication(databasePath, true);
    QVERIFY2(controller->initialize(&error), qPrintable(error));
    QVERIFY(controller->automationMode());
    QCOMPARE(controller->simpleUi(), simpleUi);
    QQmlApplicationEngine engine;
    engine.load(QUrl(simpleUi ? QStringLiteral("qrc:/qml/SimpleMain.qml")
                             : QStringLiteral("qrc:/qml/Main.qml")));
    QTRY_VERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window != nullptr);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QTRY_COMPARE(QGuiApplication::focusWindow(), window);
    QSignalSpy routeSpy(window, SIGNAL(currentRouteChanged()));
    QVERIFY(routeSpy.isValid());
    QSignalSpy historyReset(controller->watchHistory(), &QAbstractItemModel::modelReset);
    QSignalSpy nextReset(controller->watchNext(), &QAbstractItemModel::modelReset);
    auto checkRoute = [&](bool historyExpected, bool nextExpected) {
        QTRY_COMPARE(window->property("historyOpen").toBool(), historyExpected);
        QTRY_COMPARE(window->property("watchNextOpen").toBool(), nextExpected);
        QQuickItem *contentRoot = window->contentItem();
        QQuickItem *historyLoader = firstVisualChild(contentRoot, QStringLiteral("historyLoader"));
        QQuickItem *watchNextLoader = firstVisualChild(contentRoot, QStringLiteral("watchNextLoader"));
        QVERIFY(historyLoader != nullptr);
        QVERIFY(watchNextLoader != nullptr);
        QTRY_COMPARE(historyLoader->property("active").toBool(), historyExpected);
        QTRY_COMPARE(watchNextLoader->property("active").toBool(), nextExpected);
        QTRY_COMPARE(firstVisualChild(contentRoot, QStringLiteral("historyPage")) != nullptr,
                     historyExpected);
        QTRY_COMPARE(firstVisualChild(contentRoot, QStringLiteral("watchNextPage")) != nullptr,
                     nextExpected);
    };
    checkRoute(false, false);
    QTest::keyClick(window, Qt::Key_H);
    checkRoute(true, false);
    QTest::keyClick(window, Qt::Key_W);
    checkRoute(false, true);
    QTest::keyClick(window, Qt::Key_H);
    checkRoute(true, false);
    QTest::keyClick(window, Qt::Key_H);
    checkRoute(false, false);
    QTest::keyClick(window, Qt::Key_W);
    checkRoute(false, true);
    QTest::keyClick(window, Qt::Key_W);
    checkRoute(false, false);
    QTest::keyClick(window, Qt::Key_H);
    checkRoute(true, false);
    QTest::keyClick(window, Qt::Key_Escape);
    checkRoute(false, false);
    QCOMPARE(routeSpy.count(), 8);
    QCOMPARE(historyReset.count(), 3);
    QCOMPARE(nextReset.count(), 2);
    if (!simpleUi) {
        historyReset.clear();
        nextReset.clear();
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("historyNavigationButton")));
        checkRoute(true, false);
        QCOMPARE(historyReset.count(), 1);
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("historyNavigationButton")));
        checkRoute(true, false);
        QCOMPARE(historyReset.count(), 1);
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("watchNextNavigationButton")));
        checkRoute(false, true);
        QCOMPARE(nextReset.count(), 1);
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("watchNextNavigationButton")));
        checkRoute(false, true);
        QCOMPARE(nextReset.count(), 1);
        QCOMPARE(routeSpy.count(), 10);
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("feedNavigationButton")));
        checkRoute(false, false);
    }
    for (const auto &[key, selector] : QList<QPair<Qt::Key, QString>>{
             {Qt::Key_H, QStringLiteral("historyVideo_AUTO0000001")},
             {Qt::Key_W, QStringLiteral("watchNextVideo_AUTO0000002")}}) {
        QTest::keyClick(window, key);
        checkRoute(key == Qt::Key_H, key == Qt::Key_W);
        QTRY_VERIFY(firstVisualChild(window->contentItem(), selector) != nullptr);
        QQuickItem *card = firstVisualChild(window->contentItem(), selector);
        QTRY_VERIFY(card->width() > 0.0);
        QTRY_VERIFY(card->height() > 0.0);
        const bool textRow = simpleUi && key == Qt::Key_H;
        if (!textRow)
            QTRY_VERIFY(card->height() >= card->width() * 0.5625);
        const QPointF clickPoint(card->width() / 2.0,
                                 textRow ? card->height() / 2.0 : card->width() * 0.5625 / 2.0);
        const QPoint target = card->mapToScene(clickPoint).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target);
        QTRY_VERIFY2(controller->playerOpen(), qPrintable(selector));
        checkRoute(false, false);
        QTRY_VERIFY(firstVisualChild(window->contentItem(), QStringLiteral("automationPlayer")) != nullptr);
        clickItem(firstVisualChild(window->contentItem(), QStringLiteral("playerBackButton")));
        QTRY_VERIFY(!controller->playerOpen());
        checkRoute(false, false);
    }
    QVERIFY(!controller->refreshing());
    QVERIFY(controller->automationMode());
}

QTEST_MAIN(NavigationTest)

#include "navigation_test.moc"
