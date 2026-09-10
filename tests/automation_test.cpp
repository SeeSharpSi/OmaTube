#include "automationrunner.h"
#include "spaceholdhandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <utility>

#ifdef Q_OS_MAC
#define OMA_BUNDLE_SUFFIX 1
#endif

namespace {
int g_shotCounter = 0;

QString uniqueShotName(const char *tag)
{
    return QStringLiteral("omatube-%1-%2-%3.png")
        .arg(QString::fromLatin1(tag))
        .arg(QCoreApplication::applicationPid())
        .arg(++g_shotCounter);
}

QString writeSequenceFile(QTemporaryDir &dir, const QJsonArray &events)
{
    const QString path = dir.filePath(QStringLiteral("sequence.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(QJsonDocument(events).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QJsonObject ev(const char *type, int atMs)
{
    QJsonObject o;
    o[QStringLiteral("type")] = QString::fromLatin1(type);
    o[QStringLiteral("atMs")] = atMs;
    return o;
}

QList<QQuickItem *> findVisualChildrenByName(QQuickItem *parent, const QString &name)
{
    QList<QQuickItem *> matches;
    if (!parent)
        return matches;
    if (parent->objectName() == name)
        matches.append(parent);
    for (QQuickItem *child : parent->childItems())
        matches.append(findVisualChildrenByName(child, name));
    return matches;
}

QQuickItem *firstVisualChild(QQuickItem *parent, const QString &name)
{
    const auto matches = findVisualChildrenByName(parent, name);
    return matches.isEmpty() ? nullptr : matches.constFirst();
}

// Show, expose, and focus the window the runner startup gate requires.
bool showAndFocus(QQuickWindow *window)
{
    if (!window)
        return false;
    window->show();
    if (!QTest::qWaitForWindowExposed(window))
        return false;
    window->requestActivate();
    for (int i = 0; i < 50; ++i) {
        if (QGuiApplication::focusWindow() == window)
            return true;
        QTest::qWait(20);
    }
    return QGuiApplication::focusWindow() == window;
}

QQuickWindow *findTopWindow(const QString &name)
{
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (w->objectName() == name)
            return qobject_cast<QQuickWindow *>(w);
    }
    return nullptr;
}

struct KeyRec {
    QEvent::Type type;
    int key;
    Qt::KeyboardModifiers mods;
    QString text;
};

class KeyFilter final : public QObject
{
public:
    QList<KeyRec> recs;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto *ke = static_cast<QKeyEvent *>(event);
            recs.append({event->type(), ke->key(), ke->modifiers(), ke->text()});
        }
        return false;
    }
};

struct AppRun {
    bool started = false;
    int exitCode = -1;
    QProcess::ExitStatus status = QProcess::CrashExit;
    QString output;
};

QString appExecutable()
{
    QDir root(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    const QString rootPath = root.canonicalPath();
#ifdef OMA_BUNDLE_SUFFIX
    return rootPath + QStringLiteral("/build/yt-client.app/Contents/MacOS/yt-client");
#else
    return rootPath + QStringLiteral("/build/yt-client");
#endif
}

bool imageIsNonUniform(const QImage &image)
{
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); y += 7) {
        for (int x = 0; x < image.width(); x += 7) {
            if (image.pixel(x, y) != first)
                return true;
        }
    }
    return false;
}

const char kHarnessQml[] = R"QML(
import QtQuick
import QtQuick.Window
Window {
    id: root
    objectName: "appWindow"
    width: 400
    height: 300
    visible: true
    color: "white"
    property int shortcutCount: 0
    property var clicks: []
    Shortcut { sequence: "H"; onActivated: root.shortcutCount++ }
    TextInput {
        objectName: "input"
        x: 10; y: 10; width: 200; height: 30
    }
    MouseArea {
        objectName: "catcher"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onClicked: function(mouse) {
            root.clicks.push({button: mouse.button, x: mouse.x, y: mouse.y})
        }
        Rectangle {
            objectName: "clickTarget"
            x: 50; y: 80; width: 120; height: 60
            color: "#ff0000"
        }
        Rectangle {
            objectName: "blueBox"
            x: 250; y: 200; width: 60; height: 40
            color: "#0000ff"
        }
    }
}
)QML";

const char kSettingsQml[] = R"QML(
import QtQuick
import QtQuick.Window
Window {
    objectName: "settingsWindow"
    width: 300
    height: 200
    visible: true
    color: "lightgray"
    Rectangle {
        objectName: "settingsTarget"
        x: 20; y: 20; width: 100; height: 50
        color: "#00aa00"
    }
}
)QML";

} // namespace

class AutomationTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();
    void invalidSequences_data();
    void invalidSequences();
    void validSequencesLoad();
    void emptySequenceFinishes();
    void keyPressReleaseOrderAndShortcut();
    void shiftTextAndCtrlChord();
    void spaceHoldPressDuration_data();
    void spaceHoldPressDuration();
    void clickCoordinatesAndButtons();
    void timedExecutionNotEarly();
    void screenshotReadable();
    void screenshotRefusesOverwrite();
    void targetLookupFailures();
    void runtimeCoordinateAndVisibilityFailures();
    void modalSettingsHandling();
    void freshLookupAfterReplacement();
    void heldKeysReleasedWhenWindowGone();
    void cliInvalidCombinations();
    void e2eFullUi();
    void e2eSimpleUi();
    void e2eSettingsRootSwap();
    void e2eMissingTargetFails();
    void e2eEarlyQuitFails();
    void e2eSequenceFileErrors();

private:
    QQmlEngine *m_engine = nullptr;
    // QQmlComponent::create() windows are parentless: the engine does not own
    // them, so track and delete every window here or later tests see
    // duplicate appWindow/settingsWindow names and fail with ambiguity.
    QList<QPointer<QQuickWindow>> m_windows;
    QStringList m_tmpPngs;
    // Sandboxed XDG roots for child app processes so even error paths never
    // touch user config/cache locations.
    QTemporaryDir *m_xdgSandbox = nullptr;

    QQmlEngine *newEngine()
    {
        if (m_engine != nullptr) {
            QTest::qFail("engine leak between tests", __FILE__, __LINE__);
            return nullptr;
        }
        m_engine = new QQmlEngine(this);
        return m_engine;
    }
    QQuickWindow *showHarnessWindow(const char *qml = kHarnessQml);
    AppRun runAutomationApp(const QStringList &args, int timeoutMs);
    QString trackShot(const QString &name)
    {
        m_tmpPngs.append(QStringLiteral("/tmp/") + name);
        return m_tmpPngs.constLast();
    }
};

void AutomationTest::initTestCase()
{
    m_xdgSandbox = new QTemporaryDir();
    QVERIFY(m_xdgSandbox->isValid());
}

void AutomationTest::cleanupTestCase()
{
    delete m_xdgSandbox;
    m_xdgSandbox = nullptr;
}

void AutomationTest::cleanup()
{
    qDeleteAll(m_windows);
    m_windows.clear();
    delete m_engine;
    m_engine = nullptr;
    for (const QString &path : std::as_const(m_tmpPngs))
        QFile::remove(path);
    m_tmpPngs.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QQuickWindow *AutomationTest::showHarnessWindow(const char *qml)
{
    QQmlEngine *engine = newEngine();
    if (engine == nullptr)
        return nullptr;
    QQmlComponent component(engine);
    component.setData(qml, QUrl());
    if (component.isError()) {
        QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
        return nullptr;
    }
    QObject *root = component.create();
    if (root == nullptr) {
        QTest::qFail("harness component created null root", __FILE__, __LINE__);
        return nullptr;
    }
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (window == nullptr || !showAndFocus(window)) {
        QTest::qFail("harness window not exposed and focused", __FILE__, __LINE__);
        return nullptr;
    }
    m_windows.append(window);
    return window;
}

AppRun AutomationTest::runAutomationApp(const QStringList &args, int timeoutMs)
{
    AppRun result;
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    // Diagnostics must appear with no caller special-casing: main installs
    // its own stderr handler for sequence runs, so strip any override.
    env.remove(QStringLiteral("QT_FORCE_STDERR_LOGGING"));
    if (m_xdgSandbox != nullptr && m_xdgSandbox->isValid()) {
        env.insert(QStringLiteral("XDG_CONFIG_HOME"),
                   m_xdgSandbox->path() + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"),
                   m_xdgSandbox->path() + QStringLiteral("/data"));
        env.insert(QStringLiteral("XDG_CACHE_HOME"),
                   m_xdgSandbox->path() + QStringLiteral("/cache"));
    }
    process.setProcessEnvironment(env);
    process.start(appExecutable(), args);
    result.started = process.waitForStarted(15000);
    if (!result.started) {
        result.output = process.errorString();
        return result;
    }
    const bool done = process.waitForFinished(timeoutMs);
    result.output =
        QString::fromLocal8Bit(process.readAllStandardOutput() + process.readAllStandardError());
    if (!done) {
        process.kill();
        process.waitForFinished(5000);
        result.status = QProcess::CrashExit;
        return result;
    }
    result.exitCode = process.exitCode();
    result.status = process.exitStatus();
    return result;
}

void AutomationTest::invalidSequences_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("snippet");
    const auto row = [](const char *name, const QString &json, const QString &snippet) {
        QTest::newRow(name) << json.toUtf8() << snippet;
    };
    row("malformed", "{not json", QString());
    row("rootObject", "{}", QStringLiteral("array"));
    // The runner rejects non-array roots with a generic parse diagnostic;
    // rejection (not wording) is the contract here.
    row("rootString", "\"click\"", QString());
    row("rootNumber", "42", QString());
    row("elementNonObject", "[42]", QStringLiteral("action"));
    row("unknownType", R"([{"type":"dance","atMs":0}])", QStringLiteral("type"));
    row("missingType", R"([{"atMs":0}])", QStringLiteral("type"));
    row("missingAtMs", R"([{"type":"click","target":"a"}])", QStringLiteral("atMs"));
    row("extraField", R"([{"type":"click","atMs":0,"target":"a","zzz":1}])",
        QStringLiteral("unknown"));
    row("atMsDouble", R"([{"type":"click","atMs":1.5,"target":"a"}])", QStringLiteral("atMs"));
    row("atMsString", R"([{"type":"click","atMs":"0","target":"a"}])", QStringLiteral("atMs"));
    row("atMsNegative", R"([{"type":"click","atMs":-1,"target":"a"}])", QStringLiteral("atMs"));
    row("atMsOverflow", R"([{"type":"click","atMs":2147483648,"target":"a"}])",
        QStringLiteral("atMs"));
    row("atMsDecreasing",
        R"([{"type":"click","atMs":100,"target":"a"},{"type":"click","atMs":50,"target":"a"}])",
        QStringLiteral("atMs"));
    row("keyMissing", R"([{"type":"key_press","atMs":0}])", QStringLiteral("key"));
    row("keyUnknown", R"([{"type":"key_press","atMs":0,"key":"Frobnicate"}])",
        QStringLiteral("key"));
    row("keyEmpty", R"([{"type":"key_press","atMs":0,"key":""}])", QStringLiteral("key"));
    row("keyMultiCharUnknown", R"([{"type":"key_press","atMs":0,"key":"AB"}])",
        QStringLiteral("key"));
    row("keyOrphanRelease", R"([{"type":"key_release","atMs":0,"key":"H"}])",
        QStringLiteral("release"));
    row("keyDuplicatePress",
        R"([{"type":"key_press","atMs":0,"key":"H"},{"type":"key_press","atMs":10,"key":"H"}])",
        QStringLiteral("key"));
    row("keyBadWindow", R"([{"type":"key_press","atMs":0,"key":"H","window":"nope"}])",
        QStringLiteral("window"));
    row("clickBoth", R"([{"type":"click","atMs":0,"target":"a","x":1,"y":2}])",
        QStringLiteral("target"));
    row("clickNeither", R"([{"type":"click","atMs":0}])", QStringLiteral("target"));
    row("clickEmptyTarget", R"([{"type":"click","atMs":0,"target":""}])",
        QStringLiteral("target"));
    row("clickBadButton", R"([{"type":"click","atMs":0,"target":"a","button":"up"}])",
        QStringLiteral("button"));
    row("clickNegativeX", R"([{"type":"click","atMs":0,"x":-1,"y":2}])",
        QStringLiteral("coordinate"));
    row("clickStringX", R"([{"type":"click","atMs":0,"x":"1","y":2}])",
        QStringLiteral("coordinate"));
    row("clickBadWindow", R"([{"type":"click","atMs":0,"target":"a","window":"nope"}])",
        QStringLiteral("window"));
    row("screenshotMissing", R"([{"type":"screenshot","atMs":0}])",
        QStringLiteral("filename"));
    row("screenshotTraversal", R"([{"type":"screenshot","atMs":0,"filename":"../evil.png"}])",
        QStringLiteral("filename"));
    row("screenshotAbsolute", R"([{"type":"screenshot","atMs":0,"filename":"/tmp/evil.png"}])",
        QStringLiteral("filename"));
    row("screenshotBackslash", R"([{"type":"screenshot","atMs":0,"filename":"a\\b.png"}])",
        QStringLiteral("filename"));
    row("screenshotEmpty", R"([{"type":"screenshot","atMs":0,"filename":""}])",
        QStringLiteral("filename"));
    row("screenshotDuplicate",
        R"([{"type":"screenshot","atMs":0,"filename":"dup-seq.png"},{"type":"screenshot","atMs":10,"filename":"dup-seq.png"}])",
        QStringLiteral("duplicate"));
    row("screenshotBadWindow",
        R"([{"type":"screenshot","atMs":0,"filename":"x.png","window":"nope"}])",
        QStringLiteral("window"));
    row("keyExtraField", R"([{"type":"key_press","atMs":0,"key":"H","zzz":1}])",
        QStringLiteral("unknown"));
    {
        QString nulName;
        nulName.append(QChar(u'\0'));
        nulName += QStringLiteral("x.png");
        QJsonObject bad;
        bad[QStringLiteral("type")] = QStringLiteral("screenshot");
        bad[QStringLiteral("atMs")] = 0;
        bad[QStringLiteral("filename")] = nulName;
        QJsonDocument doc(QJsonArray{bad});
        QTest::newRow("screenshotNul") << doc.toJson(QJsonDocument::Compact)
                                       << QStringLiteral("filename");
    }
}

void AutomationTest::invalidSequences()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, snippet);
    AutomationRunner runner;
    QString error;
    QVERIFY2(!runner.loadSequence(json, &error), qPrintable(json));
    QVERIFY2(!error.isEmpty(), qPrintable(json));
    if (!snippet.isEmpty())
        QVERIFY2(error.contains(snippet, Qt::CaseInsensitive),
                 qPrintable(error + QStringLiteral(" / ") + QString::fromUtf8(json)));
}

void AutomationTest::validSequencesLoad()
{
    const QStringList valid{
        QStringLiteral("[]"),
        QStringLiteral(R"([{"type":"click","atMs":0,"target":"a"}])"),
        QStringLiteral(R"([{"type":"click","atMs":0,"x":0,"y":0}])"),
        QStringLiteral(
            R"([{"type":"click","atMs":0,"target":"a","button":"right","window":"appWindow"}])"),
        QStringLiteral(R"([{"type":"click","atMs":0,"x":3,"y":4,"button":"middle"}])"),
        QStringLiteral(
            R"([{"type":"key_press","atMs":0,"key":"Shift"},{"type":"key_press","atMs":0,"key":"a"},{"type":"key_release","atMs":10,"key":"a"},{"type":"key_release","atMs":10,"key":"Shift"}])"),
        QStringLiteral(
            R"([{"type":"key_press","atMs":0,"key":"H","window":"settingsWindow"}])"),
        QStringLiteral(
            R"([{"type":"key_press","atMs":0,"key":"Escape"},{"type":"key_press","atMs":0,"key":"F1"}])"),
        QStringLiteral(
            R"([{"type":"key_press","atMs":0,"key":"+"},{"type":"key_release","atMs":10,"key":"+"}])"),
        QStringLiteral(R"([{"type":"screenshot","atMs":2147483647,"filename":"ok-seq.png"}])"),
    };
    for (const QString &json : valid) {
        AutomationRunner runner;
        QString error;
        QVERIFY2(runner.loadSequence(json.toUtf8(), &error),
                 qPrintable(json + QStringLiteral(": ") + error));
    }
}

void AutomationTest::emptySequenceFinishes()
{
    // The startup gate applies even to empty sequences.
    QVERIFY(showHarnessWindow() != nullptr);
    AutomationRunner runner;
    QString error;
    QVERIFY(runner.loadSequence("[]", &error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    QVERIFY(finished.isValid() && actions.isValid());
    runner.start();
    // An empty sequence finishes synchronously inside start(); only wait if
    // the signal has not been emitted yet.
    if (finished.isEmpty())
        QVERIFY(finished.wait(10000));
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.constFirst().at(0).toBool());
    QVERIFY(actions.isEmpty());
    QVERIFY(runner.isFinished());
}

void AutomationTest::keyPressReleaseOrderAndShortcut()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    QJsonArray seq;
    const auto key = [](const char *t, int at, const char *k) {
        QJsonObject o = ev(t, at);
        o[QStringLiteral("key")] = QString::fromLatin1(k);
        return o;
    };
    seq.append(key("key_press", 0, "H"));
    seq.append(key("key_release", 60, "H"));
    // Second cycle proves no stuck autorepeat or held state: the Shortcut
    // fires exactly once per press.
    seq.append(key("key_press", 200, "H"));
    seq.append(key("key_release", 260, "H"));
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY(finished.constFirst().at(0).toBool());
    QCOMPARE(actions.count(), 4);
    for (int i = 0; i < 4; ++i)
        QCOMPARE(actions.at(i).at(0).toInt(), i);
    for (int i = 1; i < 4; ++i) {
        QVERIFY(actions.at(i).at(1).toLongLong()
                >= actions.at(i - 1).at(1).toLongLong());
    }
    // Real Shortcut activation, exactly once per press cycle. (Raw
    // KeyPress/KeyRelease objects are not observable through event filters
    // on this platform: even stock QTest::keyClick surfaces as releases at
    // filter level while shortcuts and typing still work. Behavior below
    // proves press and release delivery instead.)
    QCOMPARE(window->property("shortcutCount").toInt(), 2);
    QVERIFY(runner.isFinished());
}

void AutomationTest::shiftTextAndCtrlChord()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    // Focus the input so printable keys produce text; the plain key test
    // leaves focus clear so the window Shortcut fires instead.
    QQuickItem *input = firstVisualChild(window->contentItem(), QStringLiteral("input"));
    QVERIFY(input != nullptr);
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());
    KeyFilter filter;
    window->installEventFilter(&filter);
    QJsonArray seq;
    const auto key = [](const char *t, int at, const char *k) {
        QJsonObject o = ev(t, at);
        o[QStringLiteral("key")] = QString::fromLatin1(k);
        return o;
    };
    seq.append(key("key_press", 0, "Shift"));
    seq.append(key("key_press", 50, "a"));
    seq.append(key("key_release", 100, "a"));
    seq.append(key("key_release", 150, "Shift"));
    seq.append(key("key_press", 200, "Ctrl"));
    seq.append(key("key_press", 250, "c"));
    seq.append(key("key_release", 300, "c"));
    seq.append(key("key_release", 350, "Ctrl"));
    // Lowercase after Shift release proves the modifier release took effect.
    // Uppercase identifier "A" with no Shift held types lowercase "a".
    seq.append(key("key_press", 400, "A"));
    seq.append(key("key_release", 450, "A"));
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    QCOMPARE(actions.count(), 10);
    QCOMPARE(input->property("text").toString(), QStringLiteral("Aa"));
    bool sawShiftA = false;
    bool sawCtrlC = false;
    for (const KeyRec &rec : std::as_const(filter.recs)) {
        if (rec.type == QEvent::KeyPress && rec.key == Qt::Key_A
            && (rec.mods & Qt::ShiftModifier))
            sawShiftA = true;
        if (rec.type == QEvent::KeyPress && rec.key == Qt::Key_C
            && (rec.mods & Qt::ControlModifier))
            sawCtrlC = true;
    }
    QVERIFY(sawShiftA);
    QVERIFY(sawCtrlC);
    QVERIFY(runner.isFinished());
}

void AutomationTest::spaceHoldPressDuration_data()
{
    QTest::addColumn<int>("releaseMs");
    QTest::addColumn<bool>("expectHeld");
    QTest::addColumn<bool>("expectTap");
    QTest::newRow("tap") << 80 << false << true;
    QTest::newRow("hold") << 350 << true << false;
}

void AutomationTest::spaceHoldPressDuration()
{
    QFETCH(int, releaseMs);
    QFETCH(bool, expectHeld);
    QFETCH(bool, expectTap);
    QVERIFY(showHarnessWindow() != nullptr);
    // Space matches no Shortcut in the scene, so the raw press reaches the
    // app-level handler just like the Shift/A/C presses observed before.
    SpaceHoldHandler handler;
    QSignalSpy tapped(&handler, &SpaceHoldHandler::tapped);
    QSignalSpy heldChanged(&handler, &SpaceHoldHandler::heldChanged);
    QVERIFY(tapped.isValid() && heldChanged.isValid());
    bool sawHeld = false;
    QObject::connect(&handler, &SpaceHoldHandler::heldChanged, [&]() {
        sawHeld = sawHeld || handler.held();
    });
    QJsonArray seq;
    const auto key = [](const char *t, int at, const char *k) {
        QJsonObject o = ev(t, at);
        o[QStringLiteral("key")] = QString::fromLatin1(k);
        return o;
    };
    seq.append(key("key_press", 0, "Space"));
    QString midShot;
    if (expectHeld) {
        // Intervening capture proves the press persists across other actions.
        midShot = uniqueShotName("hold-mid");
        trackShot(midShot);
        QJsonObject shot = ev("screenshot", 150);
        shot[QStringLiteral("filename")] = midShot;
        seq.append(shot);
    }
    seq.append(key("key_release", releaseMs, "Space"));
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    QCOMPARE(tapped.count(), expectTap ? 1 : 0);
    QCOMPARE(sawHeld, expectHeld);
    if (expectHeld)
        QVERIFY(QFile::exists(QStringLiteral("/tmp/") + midShot));
    QVERIFY(runner.isFinished());
}

void AutomationTest::clickCoordinatesAndButtons()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    QJsonArray seq;
    const auto click = [](int at, const QJsonObject &extra) {
        QJsonObject o = ev("click", at);
        for (auto it = extra.begin(); it != extra.end(); ++it)
            o[it.key()] = it.value();
        return o;
    };
    seq.append(click(0, {{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}}));
    seq.append(click(80,
                    {{QStringLiteral("x"), 10},
                     {QStringLiteral("y"), 20},
                     {QStringLiteral("button"), QStringLiteral("right")}}));
    seq.append(click(160,
                    {{QStringLiteral("x"), 30},
                     {QStringLiteral("y"), 40},
                     {QStringLiteral("button"), QStringLiteral("middle")}}));
    seq.append(click(240, {{QStringLiteral("target"), QStringLiteral("clickTarget")}}));
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    QCOMPARE(actions.count(), 4);
    const QVariantList clicks = window->property("clicks").toList();
    QCOMPARE(clicks.size(), 4);
    QCOMPARE(clicks.at(0).toMap().value(QStringLiteral("x")).toInt(), 0);
    QCOMPARE(clicks.at(0).toMap().value(QStringLiteral("y")).toInt(), 0);
    QCOMPARE(clicks.at(0).toMap().value(QStringLiteral("button")).toInt(),
             static_cast<int>(Qt::LeftButton));
    QCOMPARE(clicks.at(1).toMap().value(QStringLiteral("button")).toInt(),
             static_cast<int>(Qt::RightButton));
    QCOMPARE(clicks.at(2).toMap().value(QStringLiteral("button")).toInt(),
             static_cast<int>(Qt::MiddleButton));
    QCOMPARE(clicks.at(2).toMap().value(QStringLiteral("x")).toInt(), 30);
    QCOMPARE(clicks.at(2).toMap().value(QStringLiteral("y")).toInt(), 40);
    // Target click lands inside the 120x60 rect at (50,80).
    const int tx = clicks.at(3).toMap().value(QStringLiteral("x")).toInt();
    const int ty = clicks.at(3).toMap().value(QStringLiteral("y")).toInt();
    QVERIFY(tx >= 50 && tx < 170);
    QVERIFY(ty >= 80 && ty < 140);
    QVERIFY(runner.isFinished());
}

void AutomationTest::timedExecutionNotEarly()
{
    QVERIFY(showHarnessWindow() != nullptr);
    QJsonArray seq;
    QJsonObject press = ev("key_press", 400);
    press[QStringLiteral("key")] = QStringLiteral("H");
    QJsonObject release = ev("key_release", 800);
    release[QStringLiteral("key")] = QStringLiteral("H");
    seq.append(press);
    seq.append(release);
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    runner.start();
    QVERIFY(finished.wait(20000));
    QVERIFY(finished.constFirst().at(0).toBool());
    QCOMPARE(actions.count(), 2);
    const qint64 e0 = actions.at(0).at(1).toLongLong();
    const qint64 e1 = actions.at(1).at(1).toLongLong();
    QVERIFY2(e0 + 100 >= 400, qPrintable(QString::number(e0)));
    QVERIFY2(e1 + 100 >= 800, qPrintable(QString::number(e1)));
    QVERIFY(e1 >= e0);
    QVERIFY(e1 < 15000);
}

void AutomationTest::screenshotReadable()
{
    QVERIFY(showHarnessWindow() != nullptr);
    const QString name = uniqueShotName("read");
    const QString path = trackShot(name);
    QJsonArray seq;
    QJsonObject shot = ev("screenshot", 0);
    shot[QStringLiteral("filename")] = name;
    seq.append(shot);
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error), qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    QImageReader reader(path);
    QVERIFY2(reader.canRead(), qPrintable(path));
    const QImage image = reader.read();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() >= 100 && image.height() >= 100);
    QVERIFY(imageIsNonUniform(image));
}

void AutomationTest::screenshotRefusesOverwrite()
{
    QVERIFY(showHarnessWindow() != nullptr);
    // Preexisting regular file must not be overwritten (QFile NewOnly).
    const QString fileName = uniqueShotName("exists");
    const QString filePath = trackShot(fileName);
    {
        QFile pre(filePath);
        QVERIFY(pre.open(QIODevice::WriteOnly));
        pre.write("stale");
    }
    // Preexisting symlink must not be followed/overwritten either.
    const QString linkName = uniqueShotName("link");
    const QString linkPath = trackShot(linkName);
    QTemporaryDir targetDir;
    QVERIFY(targetDir.isValid());
    const QString targetPath = targetDir.filePath(QStringLiteral("target.bin"));
    {
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write("victim");
    }
    QVERIFY(QFile::link(targetPath, linkPath));
    QJsonArray seq;
    QJsonObject first = ev("screenshot", 0);
    first[QStringLiteral("filename")] = fileName;
    QJsonObject second = ev("screenshot", 50);
    second[QStringLiteral("filename")] = linkName;
    seq.append(first);
    seq.append(second);
    // One offending event per run: run file case, then link case.
    for (int round = 0; round < 2; ++round) {
        QJsonArray single{seq.at(round)};
        AutomationRunner runner;
        QString error;
        QVERIFY(runner.loadSequence(QJsonDocument(single).toJson(), &error));
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        QVERIFY(finished.wait(15000));
        QVERIFY2(!finished.constFirst().at(0).toBool(), qPrintable(QString::number(round)));
        QVERIFY(!finished.constFirst().at(1).toString().isEmpty());
    }
    QVERIFY(QFile::exists(filePath));
    QVERIFY(QFileInfo(linkPath).isSymLink());
    {
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("victim"));
    }
}

void AutomationTest::targetLookupFailures()
{
    QVERIFY(showHarnessWindow() != nullptr);
    const auto runClick = [](const QJsonObject &extra) {
        QJsonObject o = ev("click", 0);
        for (auto it = extra.begin(); it != extra.end(); ++it)
            o[it.key()] = it.value();
        AutomationRunner runner;
        QString loadError;
        if (!runner.loadSequence(QJsonDocument(QJsonArray{o}).toJson(), &loadError))
            return std::make_pair(false, QStringLiteral("LOAD:") + loadError);
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        if (!finished.wait(15000))
            return std::make_pair(false, QStringLiteral("TIMEOUT"));
        const bool ok = finished.constFirst().at(0).toBool();
        return std::make_pair(ok, finished.constFirst().at(1).toString());
    };
    // Missing selector fails at runtime with index/time diagnostics.
    {
        const auto res =
            runClick({{QStringLiteral("target"), QStringLiteral("noSuchItem_xyz")}});
        QVERIFY(!res.first);
        QVERIFY2(res.second.contains(QStringLiteral("noSuchItem_xyz")), qPrintable(res.second));
        QVERIFY2(res.second.contains(QStringLiteral("appWindow")), qPrintable(res.second));
        QVERIFY2(res.second.contains(QStringLiteral("action 0")), qPrintable(res.second));
        QVERIFY2(res.second.contains(QStringLiteral("atMs")), qPrintable(res.second));
    }
    // Disabled target is not actionable.
    {
        QQuickWindow *window = findTopWindow(QStringLiteral("appWindow"));
        QVERIFY(window != nullptr);
        QQuickItem *target = firstVisualChild(window->contentItem(), QStringLiteral("blueBox"));
        QVERIFY(target != nullptr);
        target->setEnabled(false);
        const auto res = runClick({{QStringLiteral("target"), QStringLiteral("blueBox")}});
        QVERIFY(!res.first);
        target->setEnabled(true);
    }
    // Ambiguous duplicate names fail instead of picking one.
    {
        QQuickWindow *window = findTopWindow(QStringLiteral("appWindow"));
        QVERIFY(window != nullptr);
        QQmlComponent dup(m_engine);
        dup.setData("import QtQuick\nRectangle { width: 10; height: 10 }\n", QUrl());
        QVERIFY2(!dup.isError(), qPrintable(dup.errorString()));
        QObject *extra = dup.create();
        QVERIFY(extra != nullptr);
        auto *item = qobject_cast<QQuickItem *>(extra);
        QVERIFY(item != nullptr);
        item->setObjectName(QStringLiteral("clickTarget"));
        item->setParentItem(window->contentItem());
        const auto res =
            runClick({{QStringLiteral("target"), QStringLiteral("clickTarget")}});
        QVERIFY(!res.first);
        delete item;
    }
}

void AutomationTest::runtimeCoordinateAndVisibilityFailures()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    const auto runEvent = [](const QJsonObject &event) {
        AutomationRunner runner;
        QString loadError;
        if (!runner.loadSequence(QJsonDocument(QJsonArray{event}).toJson(), &loadError))
            return std::make_pair(false, QStringLiteral("LOAD:") + loadError);
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        if (!finished.wait(15000))
            return std::make_pair(false, QStringLiteral("TIMEOUT"));
        const bool ok = finished.constFirst().at(0).toBool();
        return std::make_pair(ok, finished.constFirst().at(1).toString());
    };
    // Huge but finite coordinates load fine yet fail at runtime, no overflow.
    {
        QJsonObject o = ev("click", 0);
        o[QStringLiteral("x")] = 1e300;
        o[QStringLiteral("y")] = 2.0;
        const auto res = runEvent(o);
        QVERIFY(!res.first);
        QVERIFY2(res.second.contains(QStringLiteral("coordinate")), qPrintable(res.second));
        QVERIFY2(res.second.contains(QStringLiteral("action 0")), qPrintable(res.second));
    }
    // Center inside the window but outside a clipping ancestor fails.
    {
        QQmlComponent clipComp(m_engine);
        clipComp.setData("import QtQuick\nItem { x: 10; y: 200; width: 50; height: 20; clip: true\n"
                         "Rectangle { objectName: \"clippedTarget\"; x: 100; y: 0; width: 40; height: 40 } }",
                         QUrl());
        QVERIFY2(!clipComp.isError(), qPrintable(clipComp.errorString()));
        auto *clipItem = qobject_cast<QQuickItem *>(clipComp.create());
        QVERIFY(clipItem != nullptr);
        clipItem->setParentItem(window->contentItem());
        QJsonObject o = ev("click", 0);
        o[QStringLiteral("target")] = QStringLiteral("clippedTarget");
        const auto res = runEvent(o);
        QVERIFY(!res.first);
        QVERIFY2(res.second.contains(QStringLiteral("clipped")), qPrintable(res.second));
        delete clipItem;
    }
    // Hidden settingsWindow cannot serve explicit input.
    {
        QQmlComponent settingsComponent(m_engine);
        settingsComponent.setData(kSettingsQml, QUrl());
        QVERIFY2(!settingsComponent.isError(), qPrintable(settingsComponent.errorString()));
        auto *settings = qobject_cast<QQuickWindow *>(settingsComponent.create());
        QVERIFY(settings != nullptr);
        m_windows.append(settings);
        settings->hide();
        QTRY_VERIFY(!settings->isVisible());
        QJsonObject o = ev("click", 0);
        o[QStringLiteral("target")] = QStringLiteral("settingsTarget");
        o[QStringLiteral("window")] = QStringLiteral("settingsWindow");
        const auto res = runEvent(o);
        QVERIFY(!res.first);
        QVERIFY2(res.second.contains(QStringLiteral("not visible")), qPrintable(res.second));
        QVERIFY2(res.second.contains(QStringLiteral("action 0")), qPrintable(res.second));
    }
}

void AutomationTest::modalSettingsHandling()
{
    QQuickWindow *app = showHarnessWindow();
    QVERIFY(app != nullptr);
    Q_UNUSED(app);
    QQmlComponent settingsComponent(m_engine);
    settingsComponent.setData(kSettingsQml, QUrl());
    QVERIFY2(!settingsComponent.isError(), qPrintable(settingsComponent.errorString()));
    QObject *settingsRoot = settingsComponent.create();
    QVERIFY(settingsRoot != nullptr);
    auto *settings = qobject_cast<QQuickWindow *>(settingsRoot);
    QVERIFY(settings != nullptr);
    m_windows.append(settings);
    // Explicit S press/release pinned to appWindow: the release must reach
    // the original press recipient even though modal settings appears
    // between the two events.
    {
        settings->hide();
        QTRY_VERIFY(!settings->isVisible());
        QJsonArray seq;
        QJsonObject press = ev("key_press", 0);
        press[QStringLiteral("key")] = QStringLiteral("S");
        press[QStringLiteral("window")] = QStringLiteral("appWindow");
        QJsonObject release = ev("key_release", 1200);
        release[QStringLiteral("key")] = QStringLiteral("S");
        release[QStringLiteral("window")] = QStringLiteral("appWindow");
        seq.append(press);
        seq.append(release);
        AutomationRunner runner;
        QString error;
        QVERIFY2(runner.loadSequence(QJsonDocument(seq).toJson(), &error),
                 qPrintable(error));
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
        runner.start();
        QVERIFY(actions.wait(10000));
        QCOMPARE(actions.count(), 1);
        settings->setModality(Qt::ApplicationModal);
        QVERIFY(showAndFocus(settings));
        if (finished.isEmpty())
            QVERIFY(finished.wait(15000));
        QVERIFY2(finished.constFirst().at(0).toBool(),
                 qPrintable(finished.constFirst().at(1).toString()));
        QVERIFY(runner.isFinished());
    }
    settings->setModality(Qt::ApplicationModal);
    QVERIFY(showAndFocus(settings));
    // Default window prefers visible modal settings: no window field needed.
    {
        QJsonObject o = ev("click", 0);
        o[QStringLiteral("target")] = QStringLiteral("settingsTarget");
        AutomationRunner runner;
        QString error;
        QVERIFY2(runner.loadSequence(QJsonDocument(QJsonArray{o}).toJson(), &error),
                 qPrintable(error));
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        QVERIFY(finished.wait(15000));
        QVERIFY2(finished.constFirst().at(0).toBool(),
                 qPrintable(finished.constFirst().at(1).toString()));
    }
    // Explicit appWindow input is blocked while modal settings is visible.
    {
        QJsonObject o = ev("click", 0);
        o[QStringLiteral("target")] = QStringLiteral("clickTarget");
        o[QStringLiteral("window")] = QStringLiteral("appWindow");
        AutomationRunner runner;
        QString error;
        QVERIFY2(runner.loadSequence(QJsonDocument(QJsonArray{o}).toJson(), &error),
                 qPrintable(error));
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        QVERIFY(finished.wait(15000));
        QVERIFY(!finished.constFirst().at(0).toBool());
        QVERIFY2(finished.constFirst().at(1).toString().contains(QStringLiteral("modal"),
                                                               Qt::CaseInsensitive),
                 qPrintable(finished.constFirst().at(1).toString()));
    }
    // Capture is not input: explicit appWindow screenshot succeeds while the
    // modal settings window is visible.
    {
        const QString name = uniqueShotName("modal-app");
        const QString path = trackShot(name);
        QJsonObject o = ev("screenshot", 0);
        o[QStringLiteral("filename")] = name;
        o[QStringLiteral("window")] = QStringLiteral("appWindow");
        AutomationRunner runner;
        QString error;
        QVERIFY2(runner.loadSequence(QJsonDocument(QJsonArray{o}).toJson(), &error),
                 qPrintable(error));
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        runner.start();
        QVERIFY(finished.wait(15000));
        QVERIFY2(finished.constFirst().at(0).toBool(),
                 qPrintable(finished.constFirst().at(1).toString()));
        QImageReader reader(path);
        QVERIFY2(reader.canRead(), qPrintable(path));
        QVERIFY(!reader.read().isNull());
    }
}

void AutomationTest::freshLookupAfterReplacement()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    QQuickItem *oldTarget =
        firstVisualChild(window->contentItem(), QStringLiteral("clickTarget"));
    QVERIFY(oldTarget != nullptr);
    oldTarget->deleteLater();
    QTRY_VERIFY(firstVisualChild(window->contentItem(), QStringLiteral("clickTarget"))
                == nullptr);
    QQmlComponent replacement(m_engine);
    replacement.setData("import QtQuick\nRectangle { x: 60; y: 90; width: 40; height: 40 }\n",
                        QUrl());
    QVERIFY2(!replacement.isError(), qPrintable(replacement.errorString()));
    auto *item = qobject_cast<QQuickItem *>(replacement.create());
    QVERIFY(item != nullptr);
    item->setObjectName(QStringLiteral("clickTarget"));
    item->setParentItem(window->contentItem());
    QTRY_VERIFY(item->window() == window);
    QJsonObject o = ev("click", 0);
    o[QStringLiteral("target")] = QStringLiteral("clickTarget");
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(QJsonArray{o}).toJson(), &error),
             qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    runner.start();
    QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    const QVariantList clicks = window->property("clicks").toList();
    QVERIFY(!clicks.isEmpty());
    QCOMPARE(clicks.constLast().toMap().value(QStringLiteral("x")).toInt(), 80);
    QCOMPARE(clicks.constLast().toMap().value(QStringLiteral("y")).toInt(), 110);
}

void AutomationTest::heldKeysReleasedWhenWindowGone()
{
    QQuickWindow *window = showHarnessWindow();
    QVERIFY(window != nullptr);
    KeyFilter filter;
    window->installEventFilter(&filter);
    // Press with no matching release: completion must auto-release held input.
    QJsonObject press = ev("key_press", 0);
    press[QStringLiteral("key")] = QStringLiteral("Shift");
    AutomationRunner runner;
    QString error;
    QVERIFY2(runner.loadSequence(QJsonDocument(QJsonArray{press}).toJson(), &error),
             qPrintable(error));
    QSignalSpy finished(&runner, &AutomationRunner::finished);
    QSignalSpy actions(&runner, &AutomationRunner::actionExecuted);
    runner.start();
    QVERIFY(actions.wait(10000));
    // Original window disappears while the modifier is still held.
    qDeleteAll(m_windows);
    m_windows.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // Completion is emitted in the same step as the last action, possibly
    // before this wait begins.
    if (finished.isEmpty())
        QVERIFY(finished.wait(15000));
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(1).toString()));
    QVERIFY(runner.isFinished());
}

void AutomationTest::cliInvalidCombinations()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonArray oneClick{QJsonObject{
        {QStringLiteral("type"), QStringLiteral("screenshot")},
        {QStringLiteral("atMs"), 0},
        {QStringLiteral("filename"), uniqueShotName("cli")}}};
    const QString seqFile = writeSequenceFile(dir, oneClick);
    QVERIFY(!seqFile.isEmpty());
    // --automation rejects --database. Non-sequence CLI diagnostics may go
    // to the container journal, so only the exit code is asserted here.
    {
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation"), QStringLiteral("--database"),
             dir.filePath(QStringLiteral("user.sqlite3"))},
            30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
    // Sequence is incompatible with --database.
    {
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation-sequence"), seqFile, QStringLiteral("--database"),
             dir.filePath(QStringLiteral("user.sqlite3"))},
            30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
    // Sequence is incompatible with --quit-after-startup.
    {
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation-sequence"), seqFile,
             QStringLiteral("--quit-after-startup")},
            30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
    // --automation-ui requires automation mode.
    {
        const AppRun run =
            runAutomationApp({QStringLiteral("--automation-ui"), QStringLiteral("simple")},
                             30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
    // Unknown UI flavor is rejected.
    {
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation"), QStringLiteral("--automation-ui"),
             QStringLiteral("fancy")},
            30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
    // Missing sequence file fails fast without opening windows.
    {
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation-sequence"),
             dir.filePath(QStringLiteral("does-not-exist.json"))},
            30000);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    }
}

void AutomationTest::e2eFullUi()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    const QString shotPlayer = uniqueShotName("full-player");
    const QString shotFeed = uniqueShotName("full-feed");
    trackShot(shotPlayer);
    trackShot(shotFeed);
    const auto click = [](int at, const QString &target) {
        QJsonObject o = ev("click", at);
        o[QStringLiteral("target")] = target;
        return o;
    };
    QJsonArray seq;
    seq.append(click(800, QStringLiteral("categoryButton_2")));
    seq.append(click(2000, QStringLiteral("feedVideo_AUTO0000004")));
    QJsonObject shot1 = ev("screenshot", 3600);
    shot1[QStringLiteral("filename")] = shotPlayer;
    seq.append(shot1);
    seq.append(click(5000, QStringLiteral("playerBackButton")));
    QJsonObject shot2 = ev("screenshot", 6600);
    shot2[QStringLiteral("filename")] = shotFeed;
    seq.append(shot2);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString seqFile = writeSequenceFile(dir, seq);
    QVERIFY(!seqFile.isEmpty());
    // --automation-sequence implies --automation; no explicit flag needed.
    const AppRun run =
        runAutomationApp({QStringLiteral("--automation-sequence"), seqFile}, 90000);
    QVERIFY(run.started);
    QVERIFY2(run.status == QProcess::NormalExit && run.exitCode == 0, qPrintable(run.output));
    // The sequence input file is caller-owned and must survive the run.
    QVERIFY(QFile::exists(seqFile));
    for (const QString &path : {QStringLiteral("/tmp/") + shotPlayer,
                                QStringLiteral("/tmp/") + shotFeed}) {
        QImageReader reader(path);
        QVERIFY2(reader.canRead(), qPrintable(path));
        const QImage image = reader.read();
        QVERIFY(!image.isNull());
        QVERIFY(image.width() >= 100 && image.height() >= 100);
    }
}

void AutomationTest::e2eSimpleUi()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    const QString shot = uniqueShotName("simple-queue");
    trackShot(shot);
    const auto key = [](const char *t, int at, const char *k) {
        QJsonObject o = ev(t, at);
        o[QStringLiteral("key")] = QString::fromLatin1(k);
        return o;
    };
    QJsonArray seq;
    seq.append(key("key_press", 800, "W"));
    seq.append(key("key_release", 950, "W"));
    QJsonObject click = ev("click", 2400);
    click[QStringLiteral("target")] = QStringLiteral("watchNextVideo_AUTO0000002");
    seq.append(click);
    QJsonObject shotEv = ev("screenshot", 4000);
    shotEv[QStringLiteral("filename")] = shot;
    seq.append(shotEv);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString seqFile = writeSequenceFile(dir, seq);
    QVERIFY(!seqFile.isEmpty());
    const AppRun run = runAutomationApp(
        {QStringLiteral("--automation-ui"), QStringLiteral("simple"),
         QStringLiteral("--automation-sequence"), seqFile},
        90000);
    QVERIFY(run.started);
    QVERIFY2(run.status == QProcess::NormalExit && run.exitCode == 0, qPrintable(run.output));
    const QString path = QStringLiteral("/tmp/") + shot;
    QImageReader reader(path);
    QVERIFY2(reader.canRead(), qPrintable(path));
    const QImage image = reader.read();
    QVERIFY(!image.isNull());
    QVERIFY(imageIsNonUniform(image));
}

void AutomationTest::e2eSettingsRootSwap()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    const QString shot = uniqueShotName("swapped");
    trackShot(shot);
    const auto key = [](const char *t, int at, const char *k) {
        QJsonObject o = ev(t, at);
        o[QStringLiteral("key")] = QString::fromLatin1(k);
        return o;
    };
    const auto click = [](int at, const QString &target) {
        QJsonObject o = ev("click", at);
        o[QStringLiteral("target")] = target;
        return o;
    };
    QJsonArray seq;
    // C opens settingsWindow; the release is pinned to appWindow explicitly
    // so it must not fail on the modal gate either.
    QJsonObject sPress = key("key_press", 800, "C");
    sPress[QStringLiteral("window")] = QStringLiteral("appWindow");
    seq.append(sPress);
    QJsonObject sRelease = key("key_release", 950, "C");
    sRelease[QStringLiteral("window")] = QStringLiteral("appWindow");
    seq.append(sRelease);
    seq.append(click(2200, QStringLiteral("settingsAppearanceTab")));
    seq.append(click(3200, QStringLiteral("simpleUiCheckBox")));
    // Replacement Simple UI root: open history and open a history row.
    seq.append(key("key_press", 4800, "H"));
    seq.append(key("key_release", 4950, "H"));
    seq.append(click(6400, QStringLiteral("historyVideo_AUTO0000001")));
    QJsonObject shotEv = ev("screenshot", 8000);
    shotEv[QStringLiteral("filename")] = shot;
    seq.append(shotEv);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString seqFile = writeSequenceFile(dir, seq);
    QVERIFY(!seqFile.isEmpty());
    const AppRun run = runAutomationApp(
        {QStringLiteral("--automation"), QStringLiteral("--automation-sequence"), seqFile},
        120000);
    QVERIFY(run.started);
    QVERIFY2(run.status == QProcess::NormalExit && run.exitCode == 0, qPrintable(run.output));
    const QString path = QStringLiteral("/tmp/") + shot;
    QImageReader reader(path);
    QVERIFY2(reader.canRead(), qPrintable(path));
    const QImage image = reader.read();
    QVERIFY(!image.isNull());
    QVERIFY(image.width() >= 100 && image.height() >= 100);
}

void AutomationTest::e2eMissingTargetFails()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    QJsonArray seq;
    QJsonObject o = ev("click", 300);
    o[QStringLiteral("target")] = QStringLiteral("noSuchItem_xyz");
    seq.append(o);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString seqFile = writeSequenceFile(dir, seq);
    QVERIFY(!seqFile.isEmpty());
    const AppRun run =
        runAutomationApp({QStringLiteral("--automation-sequence"), seqFile}, 90000);
    QVERIFY(run.started);
    QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("Automation sequence failed:")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("action 0")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("noSuchItem_xyz")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("appWindow")), qPrintable(run.output));
}

void AutomationTest::e2eEarlyQuitFails()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    const QString shot = uniqueShotName("early-quit");
    trackShot(shot);
    QJsonArray seq;
    QJsonObject press = ev("key_press", 300);
    press[QStringLiteral("key")] = QStringLiteral("q");
    QJsonObject release = ev("key_release", 350);
    release[QStringLiteral("key")] = QStringLiteral("q");
    seq.append(press);
    seq.append(release);
    QJsonObject shotEv = ev("screenshot", 4000);
    shotEv[QStringLiteral("filename")] = shot;
    seq.append(shotEv);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString seqFile = writeSequenceFile(dir, seq);
    QVERIFY(!seqFile.isEmpty());
    const AppRun run = runAutomationApp(
        {QStringLiteral("--automation-ui"), QStringLiteral("simple"),
         QStringLiteral("--automation-sequence"), seqFile},
        90000);
    QVERIFY(run.started);
    QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0, qPrintable(run.output));
    // Quitting early aborts the rest of the sequence: no screenshot created.
    QVERIFY2(!QFile::exists(QStringLiteral("/tmp/") + shot), qPrintable(shot));
}

void AutomationTest::e2eSequenceFileErrors()
{
    const QString app = appExecutable();
    QVERIFY2(QFile::exists(app), qPrintable(app));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto runBadFile = [&](const QString &path) {
        QElapsedTimer clock;
        clock.start();
        const AppRun run = runAutomationApp(
            {QStringLiteral("--automation-sequence"), path}, 60000);
        return std::make_pair(run, clock.elapsed());
    };
    // Unreadable sequence file fails before any window or database work.
    {
        const QString path = dir.filePath(QStringLiteral("unreadable.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[]");
        file.close();
        QVERIFY(file.setPermissions(QFileDevice::Permissions{}));
        if (QFileInfo(path).isReadable())
            QSKIP("file stays readable despite chmod (e.g. running as root)");
        const auto [run, elapsed] = runBadFile(path);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0,
                 qPrintable(run.output));
        QVERIFY2(run.output.contains(QStringLiteral("sequence"), Qt::CaseInsensitive),
                 qPrintable(run.output));
        QVERIFY2(elapsed < 30000, qPrintable(QString::number(elapsed)));
    }
    // Malformed JSON fails the same way.
    {
        const QString path = dir.filePath(QStringLiteral("malformed.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{not json");
        file.close();
        const auto [run, elapsed] = runBadFile(path);
        QVERIFY(run.started);
        QVERIFY2(run.status == QProcess::NormalExit && run.exitCode != 0,
                 qPrintable(run.output));
        QVERIFY2(run.output.contains(QStringLiteral("sequence"), Qt::CaseInsensitive),
                 qPrintable(run.output));
        QVERIFY2(elapsed < 30000, qPrintable(QString::number(elapsed)));
    }
}

QTEST_MAIN(AutomationTest)

#include "automation_test.moc"
