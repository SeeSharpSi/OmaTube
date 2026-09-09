#include "automationrunner.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/qtestkeyboard.h>
#include <QtTest/qtestmouse.h>

#include <climits>
#include <cmath>

namespace {
constexpr int kStartupTimeoutMs = 5000;
constexpr int kReadyPollMs = 25;

bool isOmaWindowName(const QString &name)
{
    return name == QLatin1String("appWindow") || name == QLatin1String("settingsWindow");
}

Qt::KeyboardModifier modifierFlag(int code)
{
    if (code == Qt::Key_Shift)
        return Qt::ShiftModifier;
    if (code == Qt::Key_Control)
        return Qt::ControlModifier;
    if (code == Qt::Key_Alt)
        return Qt::AltModifier;
    if (code == Qt::Key_Meta)
        return Qt::MetaModifier;
    return Qt::NoModifier;
}

QString shiftedUs(QChar base)
{
    switch (base.unicode()) {
    case '1': return QStringLiteral("!");
    case '2': return QStringLiteral("@");
    case '3': return QStringLiteral("#");
    case '4': return QStringLiteral("$");
    case '5': return QStringLiteral("%");
    case '6': return QStringLiteral("^");
    case '7': return QStringLiteral("&");
    case '8': return QStringLiteral("*");
    case '9': return QStringLiteral("(");
    case '0': return QStringLiteral(")");
    case '-': return QStringLiteral("_");
    case '=': return QStringLiteral("+");
    case '[': return QStringLiteral("{");
    case ']': return QStringLiteral("}");
    case ';': return QStringLiteral(":");
    case '\'': return QStringLiteral("\"");
    case ',': return QStringLiteral("<");
    case '.': return QStringLiteral(">");
    case '/': return QStringLiteral("?");
    case '`': return QStringLiteral("~");
    case '\\': return QStringLiteral("|");
    default: return QString(base);
    }
}

struct KeyNameEntry {
    const char *name;
    Qt::Key key;
    bool modifier;
};

bool parseKey(const QString &name, Qt::Key *code, bool *isMod, QChar *base,
              bool *single, QString *detail)
{
    if (name.isEmpty()) {
        if (detail)
            *detail = QStringLiteral("key is empty");
        return false;
    }
    if (name.size() == 1) {
        const QChar c = name.at(0);
        const uint u = c.unicode();
        if (u < 0x20 || u > 0x7E) {
            if (detail)
                *detail = QStringLiteral("unsupported key");
            return false;
        }
        // ASCII Qt::Key values equal the upper-case code point directly.
        *code = c.isLetter() ? static_cast<Qt::Key>(c.toUpper().unicode())
                             : static_cast<Qt::Key>(u);
        *isMod = false;
        *base = c;
        *single = true;
        return true;
    }
    static const KeyNameEntry table[] = {
        {"escape", Qt::Key_Escape, false}, {"space", Qt::Key_Space, false},
        {"return", Qt::Key_Return, false}, {"enter", Qt::Key_Enter, false},
        {"tab", Qt::Key_Tab, false}, {"backspace", Qt::Key_Backspace, false},
        {"delete", Qt::Key_Delete, false}, {"insert", Qt::Key_Insert, false},
        {"home", Qt::Key_Home, false}, {"end", Qt::Key_End, false},
        {"pageup", Qt::Key_PageUp, false}, {"pagedown", Qt::Key_PageDown, false},
        {"left", Qt::Key_Left, false}, {"up", Qt::Key_Up, false},
        {"right", Qt::Key_Right, false}, {"down", Qt::Key_Down, false},
        {"shift", Qt::Key_Shift, true}, {"control", Qt::Key_Control, true},
        {"ctrl", Qt::Key_Control, true}, {"alt", Qt::Key_Alt, true},
        {"meta", Qt::Key_Meta, true},
    };
    const QString lower = name.toLower();
    for (const KeyNameEntry &e : table) {
        if (lower == QLatin1String(e.name)) {
            *code = e.key;
            *isMod = e.modifier;
            *base = QChar();
            *single = false;
            return true;
        }
    }
    if (lower.size() >= 2 && lower.size() <= 3 && lower.at(0) == QLatin1Char('f')) {
        bool digits = true;
        for (int i = 1; i < lower.size(); ++i) {
            if (!lower.at(i).isDigit())
                digits = false;
        }
        bool ok = false;
        const int n = lower.mid(1).toInt(&ok);
        if (ok && digits && n >= 1 && n <= 35) {
            *code = static_cast<Qt::Key>(Qt::Key_F1 + (n - 1));
            *isMod = false;
            *base = QChar();
            *single = false;
            return true;
        }
    }
    if (detail)
        *detail = QStringLiteral("unsupported key");
    return false;
}

bool valueIsIntMs(const QJsonValue &v, qint64 *out)
{
    if (!v.isDouble())
        return false;
    const double dv = v.toDouble();
    if (!std::isfinite(dv) || std::floor(dv) != dv)
        return false;
    if (dv < 0.0 || dv > static_cast<double>(INT_MAX))
        return false;
    *out = static_cast<qint64>(dv);
    return true;
}
} // namespace

AutomationRunner::AutomationRunner(QObject *parent)
    : QObject(parent)
{
    m_readyTimer.setSingleShot(false);
    m_readyTimer.setTimerType(Qt::PreciseTimer);
    m_readyTimer.setInterval(kReadyPollMs);
    m_stepTimer.setSingleShot(true);
    m_stepTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_readyTimer, &QTimer::timeout, this, &AutomationRunner::onReadyPoll);
    connect(&m_stepTimer, &QTimer::timeout, this, &AutomationRunner::executeCurrent);
}

AutomationRunner::~AutomationRunner()
{
    m_readyTimer.stop();
    m_stepTimer.stop();
}

bool AutomationRunner::isFinished() const
{
    return m_finished;
}

Qt::KeyboardModifiers AutomationRunner::currentModifiers() const
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    for (int k : m_held)
        mods |= modifierFlag(k);
    return mods;
}

QString AutomationRunner::dispatchText(const Action &a) const
{
    const bool shift = m_held.contains(Qt::Key_Shift);
    if (a.isSingleChar) {
        if (a.baseChar.isLetter())
            return shift ? QString(a.baseChar.toUpper()) : QString(a.baseChar.toLower());
        if (!shift)
            return QString(a.baseChar);
        return shiftedUs(a.baseChar);
    }
    if (a.keyCode == Qt::Key_Space)
        return QStringLiteral(" ");
    if (a.keyCode == Qt::Key_Return || a.keyCode == Qt::Key_Enter)
        return QStringLiteral("\r");
    if (a.keyCode == Qt::Key_Tab)
        return QStringLiteral("\t");
    return QString();
}

void AutomationRunner::collectByName(QQuickItem *parent, const QString &name,
                                     QList<QQuickItem *> &out)
{
    if (!parent)
        return;
    if (parent->objectName() == name)
        out.append(parent);
    for (QQuickItem *child : parent->childItems())
        collectByName(child, name, out);
}

QQuickWindow *AutomationRunner::findVisibleNamed(const QString &name,
                                                 bool *ambiguous) const
{
    QQuickWindow *found = nullptr;
    bool dup = false;
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        auto *quick = qobject_cast<QQuickWindow *>(w);
        if (!quick || quick->objectName() != name || !quick->isVisible())
            continue;
        if (found)
            dup = true;
        else
            found = quick;
    }
    if (ambiguous)
        *ambiguous = dup;
    return dup ? nullptr : found;
}

bool AutomationRunner::settingsBlocksInput() const
{
    bool amb = false;
    QQuickWindow *s = findVisibleNamed(QStringLiteral("settingsWindow"), &amb);
    if (amb)
        return true;
    if (!s)
        return false;
    return s->modality() != Qt::NonModal;
}

// forInput gates explicit-appWindow input behind a visible modal settings
// window; capture never applies that gate.
QQuickWindow *AutomationRunner::resolveWindow(const QString &requested,
                                              QString *error, bool forInput) const
{
    if (!requested.isEmpty()) {
        if (!isOmaWindowName(requested)) {
            if (error)
                *error = QStringLiteral("unknown window '%1'").arg(requested);
            return nullptr;
        }
        if (forInput && requested == QLatin1String("appWindow")
            && settingsBlocksInput()) {
            if (error)
                *error = QStringLiteral("explicit appWindow blocked by visible modal settingsWindow '%1'").arg(requested);
            return nullptr;
        }
        bool amb = false;
        QQuickWindow *w = findVisibleNamed(requested, &amb);
        if (amb) {
            if (error)
                *error = QStringLiteral("window '%1' is ambiguous").arg(requested);
            return nullptr;
        }
        if (!w) {
            if (error)
                *error = QStringLiteral("window '%1' not found or not visible").arg(requested);
            return nullptr;
        }
        if (!w->isExposed()) {
            if (error)
                *error = QStringLiteral("window '%1' not visible/exposed").arg(requested);
            return nullptr;
        }
        return w;
    }
    if (settingsBlocksInput()) {
        bool amb = false;
        QQuickWindow *s = findVisibleNamed(QStringLiteral("settingsWindow"), &amb);
        if (s && !amb && s->isExposed())
            return s;
        if (error)
            *error = QStringLiteral("modal settingsWindow not ready");
        return nullptr;
    }
    if (auto *qf = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow())) {
        if (isOmaWindowName(qf->objectName()) && qf->isVisible() && qf->isExposed())
            return qf;
    }
    bool amb = false;
    if (!forInput) {
        QQuickWindow *s = findVisibleNamed(QStringLiteral("settingsWindow"), &amb);
        if (s && !amb && s->isExposed())
            return s;
    }
    QQuickWindow *app = findVisibleNamed(QStringLiteral("appWindow"), &amb);
    if (app && !amb && app->isExposed())
        return app;
    if (error)
        *error = QStringLiteral("no visible OmaTube window");
    return nullptr;
}

bool AutomationRunner::appReady() const
{
    bool amb = false;
    QQuickWindow *app = findVisibleNamed(QStringLiteral("appWindow"), &amb);
    if (!app || amb || !app->isExposed() || !app->isSceneGraphInitialized()
        || !app->contentItem())
        return false;
    auto *qf = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow());
    if (!qf || !qf->isVisible())
        return false;
    if (qf->objectName() == QLatin1String("appWindow"))
        return true;
    bool sAmb = false;
    QQuickWindow *s = findVisibleNamed(QStringLiteral("settingsWindow"), &sAmb);
    return s && !sAmb && qf == s;
}

void AutomationRunner::onReadyPoll()
{
    if (m_finished)
        return;
    if (appReady()) {
        m_readyTimer.stop();
        m_clock.start();
        m_clockRunning = true;
        m_next = 0;
        if (m_actions.isEmpty()) {
            finishSuccess();
            return;
        }
        scheduleNext();
        return;
    }
    if (m_startupClock.elapsed() > kStartupTimeoutMs) {
        m_readyTimer.stop();
        finishFailure(QStringLiteral("startup timeout: appWindow not ready within 5000ms"));
    }
}

void AutomationRunner::scheduleNext()
{
    if (m_finished || m_next >= m_actions.size())
        return;
    const Action &a = m_actions.at(m_next);
    qint64 delay = a.atMs - (m_clockRunning ? m_clock.elapsed() : 0);
    if (delay < 0)
        delay = 0;
    m_stepTimer.start(static_cast<int>(qMin<qint64>(delay, INT_MAX)));
}

void AutomationRunner::executeCurrent()
{
    if (m_finished || m_next >= m_actions.size()) {
        if (!m_finished)
            finishSuccess();
        return;
    }
    m_stepTimer.stop();
    Action a = m_actions.at(m_next);
    if (m_clockRunning && m_clock.elapsed() < a.atMs) {
        scheduleNext();
        return;
    }
    const qint64 elapsed = m_clockRunning ? m_clock.elapsed() : 0;
    QString error;
    bool ok = false;
    if (a.type == QLatin1String("key_press") || a.type == QLatin1String("key_release"))
        ok = runKey(a, &error);
    else if (a.type == QLatin1String("click"))
        ok = runClick(a, &error);
    else if (a.type == QLatin1String("screenshot"))
        ok = runScreenshot(a, &error);
    else
        error = QStringLiteral("unknown type");
    if (!ok) {
        finishFailure(QStringLiteral("action %1 (atMs %2): %3").arg(a.index).arg(a.atMs).arg(error));
        return;
    }
    ++m_next;
    qInfo().noquote() << "automation action" << a.index << "executed at" << elapsed
                      << "ms (scheduled" << a.atMs << "ms)";
    emit actionExecuted(a.index, elapsed);
    if (m_next >= m_actions.size())
        finishSuccess();
    else
        scheduleNext();
}

void AutomationRunner::finishFailure(const QString &error)
{
    if (m_finished)
        return;
    m_finished = true;
    m_readyTimer.stop();
    m_stepTimer.stop();
    releaseHeldSilently();
    emit finished(false, error);
}

void AutomationRunner::finishSuccess()
{
    if (m_finished)
        return;
    m_finished = true;
    m_readyTimer.stop();
    m_stepTimer.stop();
    releaseHeldSilently();
    emit finished(true, QString());
}

void AutomationRunner::releaseHeldSilently()
{
    const QList<int> keys = m_held.values();
    for (int code : keys) {
        QPointer<QWindow> rec = m_pressWindows.value(code);
        auto *qw = rec ? qobject_cast<QQuickWindow *>(rec.data()) : nullptr;
        if (!qw)
            qw = resolveWindow(QString(), nullptr, false);
        if (!qw) {
            m_held.remove(code);
            m_pressWindows.remove(code);
            continue;
        }
        QTest::simulateEvent(qw, false, code,
                             currentModifiers() & ~modifierFlag(code), QString(), false);
        m_held.remove(code);
        m_pressWindows.remove(code);
    }
    m_held.clear();
}

bool AutomationRunner::runKey(const Action &a, QString *error)
{
    const bool press = (a.type == QLatin1String("key_press"));
    if (!press) {
        // Stored recipient takes priority so S release reaches appWindow even
        // after S opened the modal settings window.
        const int code = static_cast<int>(a.keyCode);
        if (m_pressWindows.contains(code)) {
            QPointer<QWindow> rec = m_pressWindows.value(code);
            auto *target = rec ? qobject_cast<QQuickWindow *>(rec.data()) : nullptr;
            if (!target)
                target = resolveWindow(QString(), nullptr, false);
            if (!target) {
                m_held.remove(code);
                m_pressWindows.remove(code);
                return true;
            }
            QTest::simulateEvent(target, false, code,
                                 currentModifiers() & ~modifierFlag(code), QString(),
                                 false);
            m_held.remove(code);
            m_pressWindows.remove(code);
            return true;
        }
        QQuickWindow *w = resolveWindow(a.windowName, error, true);
        if (!w) {
            if (error)
                *error = QStringLiteral("key '%1': %2").arg(a.keyName).arg(*error);
            return false;
        }
        QTest::simulateEvent(w, false, static_cast<int>(a.keyCode),
                             currentModifiers() & ~modifierFlag(static_cast<int>(a.keyCode)),
                             QString(), false);
        return true;
    }
    QQuickWindow *w = resolveWindow(a.windowName, error, true);
    if (!w) {
        if (error)
            *error = QStringLiteral("key '%1': %2").arg(a.keyName).arg(*error);
        return false;
    }
    QPointer<QQuickWindow> guard(w);
    // Single simulateEvent preserves explicit held-modifier state; keyPress
    // with a modifiers argument would synthesize extra modifier presses.
    QTest::simulateEvent(w, true, static_cast<int>(a.keyCode), currentModifiers(),
                         a.isModifier ? QString() : dispatchText(a), false);
    const int code = static_cast<int>(a.keyCode);
    m_held.insert(code);
    m_pressWindows.insert(code, QPointer<QWindow>(guard ? guard.data() : nullptr));
    return true;
}

void AutomationRunner::mouseClick(QQuickWindow *window, Qt::MouseButton button,
                                  Qt::KeyboardModifiers mods, const QPointF &local)
{
    QPointer<QQuickWindow> guard(window);
    const QPointF global = window->mapToGlobal(local);
    QTest::lastMouseTimestamp += 10;
    qt_handleMouseEvent(window, local, global, Qt::MouseButtons(button), button,
                        QEvent::MouseButtonPress, mods, QTest::lastMouseTimestamp);
    if (!guard)
        return;
    QTest::lastMouseTimestamp += 10;
    qt_handleMouseEvent(window, local, global, Qt::MouseButtons(Qt::NoButton), button,
                        QEvent::MouseButtonRelease, mods, QTest::lastMouseTimestamp);
    QTest::lastMouseTimestamp += QTest::mouseDoubleClickInterval;
    if (guard)
        QCoreApplication::processEvents();
}

bool AutomationRunner::runClick(const Action &a, QString *error)
{
    QQuickWindow *window = resolveWindow(a.windowName, error, true);
    if (!window)
        return false;
    QPointF pos;
    if (a.hasTarget) {
        if (!window->contentItem()) {
            if (error)
                *error = QStringLiteral("target '%1' has no content item in window '%2'").arg(a.target).arg(window->objectName());
            return false;
        }
        QList<QQuickItem *> matches;
        collectByName(window->contentItem(), a.target, matches);
        if (matches.isEmpty()) {
            if (error)
                *error = QStringLiteral("target '%1' not found in window '%2'").arg(a.target).arg(window->objectName());
            return false;
        }
        if (matches.size() > 1) {
            if (error)
                *error = QStringLiteral("target '%1' is ambiguous (%2 matches) in window '%3'").arg(a.target).arg(matches.size()).arg(window->objectName());
            return false;
        }
        QQuickItem *item = matches.first();
        bool chainOk = item->isVisible() && item->isEnabled();
        for (QQuickItem *p = item; chainOk && p && p != window->contentItem();
             p = p->parentItem()) {
            if (!p->isVisible() || !p->isEnabled())
                chainOk = false;
        }
        if (!chainOk) {
            if (error)
                *error = QStringLiteral("target '%1' not visible or not enabled in window '%2'").arg(a.target).arg(window->objectName());
            return false;
        }
        if (!(item->width() > 0.0) || !(item->height() > 0.0)) {
            if (error)
                *error = QStringLiteral("target '%1' has zero size in window '%2'").arg(a.target).arg(window->objectName());
            return false;
        }
        const QPointF sceneCenter =
            item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
        pos = window->contentItem()->mapFromScene(sceneCenter);
        const double w = static_cast<double>(window->width());
        const double h = static_cast<double>(window->height());
        if (!(pos.x() >= 0.0 && pos.y() >= 0.0 && pos.x() < w && pos.y() < h)) {
            if (error)
                *error = QStringLiteral("target '%1' center (%2,%3) outside window '%4' (%5x%6)").arg(a.target).arg(pos.x()).arg(pos.y()).arg(window->objectName()).arg(window->width()).arg(window->height());
            return false;
        }
        for (QQuickItem *p = item->parentItem(); p && p != window->contentItem();
             p = p->parentItem()) {
            if (!p->clip())
                continue;
            const QPointF lp = p->mapFromScene(sceneCenter);
            if (!(lp.x() >= 0.0 && lp.y() >= 0.0 && lp.x() <= p->width()
                  && lp.y() <= p->height())) {
                if (error)
                    *error = QStringLiteral("target '%1' clipped in ancestor of window '%2'").arg(a.target).arg(window->objectName());
                return false;
            }
        }
        item = nullptr;
    } else {
        const double w = static_cast<double>(window->width());
        const double h = static_cast<double>(window->height());
        if (!(a.x >= 0.0 && a.y >= 0.0 && a.x < w && a.y < h)) {
            if (error)
                *error = QStringLiteral("coordinate (%1,%2) outside window '%3' (%4x%5)").arg(a.x).arg(a.y).arg(window->objectName()).arg(window->width()).arg(window->height());
            return false;
        }
        pos = QPointF(a.x, a.y);
    }
    mouseClick(window, a.button, currentModifiers(), pos);
    return true;
}

bool AutomationRunner::runScreenshot(const Action &a, QString *error)
{
    QQuickWindow *window = resolveWindow(a.windowName, error, false);
    if (!window)
        return false;
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("grabWindow returned null image");
        return false;
    }
    const QString path = QStringLiteral("/tmp/%1").arg(a.filename);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error)
            *error = QStringLiteral("cannot create screenshot file '%1': %2").arg(path).arg(file.errorString());
        return false;
    }
    const bool saved = image.save(&file, "PNG");
    const bool flushed = file.flush();
    const QFileDevice::FileError writeError = file.error();
    const QString writeDetail = file.errorString();
    file.close();
    if (!saved || !flushed || writeError != QFileDevice::NoError) {
        file.remove();
        if (error) {
            *error = QStringLiteral("failed to write screenshot PNG '%1': %2").arg(path).arg(writeDetail);
        }
        return false;
    }
    qInfo().noquote() << "automation screenshot saved:" << path;
    return true;
}

bool AutomationRunner::loadSequence(const QByteArray &json, QString *error)
{
    if (m_started && !m_finished) {
        if (error)
            *error = QStringLiteral("sequence already running");
        return false;
    }
    m_actions.clear();
    m_hasSequence = false;
    m_started = false;
    m_finished = false;
    m_clockRunning = false;
    m_next = 0;
    m_held.clear();
    m_pressWindows.clear();
    m_readyTimer.stop();
    m_stepTimer.stop();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("invalid JSON: ") + parseError.errorString();
        return false;
    }
    if (!doc.isArray()) {
        if (error)
            *error = QStringLiteral("top-level JSON must be an array");
        return false;
    }
    const QJsonArray arr = doc.array();
    QList<Action> actions;
    actions.reserve(arr.size());
    QSet<QString> screenshotNames;
    QSet<int> heldCheck;
    qint64 previousAtMs = 0;

    for (int i = 0; i < arr.size(); ++i) {
        auto fail = [error, i](const QString &msg) {
            if (error)
                *error = QStringLiteral("action %1: %2").arg(i).arg(msg);
            return false;
        };
        auto failAt = [error, i](qint64 atMs, const QString &msg) {
            if (error) {
                *error = QStringLiteral("action %1 (atMs %2): %3").arg(i).arg(atMs).arg(msg);
            }
            return false;
        };
        if (!arr.at(i).isObject())
            return fail(QStringLiteral("must be an object"));
        const QJsonObject o = arr.at(i).toObject();
        if (!o.contains(QLatin1String("type")))
            return fail(QStringLiteral("missing 'type'"));
        if (!o.contains(QLatin1String("atMs")))
            return fail(QStringLiteral("missing 'atMs'"));
        if (!o.value(QLatin1String("type")).isString())
            return fail(QStringLiteral("'type' must be a string"));
        const QString type = o.value(QLatin1String("type")).toString();
        const bool isKey = (type == QLatin1String("key_press")
            || type == QLatin1String("key_release"));
        const bool isClick = (type == QLatin1String("click"));
        const bool isShot = (type == QLatin1String("screenshot"));
        if (!isKey && !isClick && !isShot)
            return fail(QStringLiteral("unknown type"));
        qint64 atMs = 0;
        if (!valueIsIntMs(o.value(QLatin1String("atMs")), &atMs))
            return fail(QStringLiteral("'atMs' must be an integer 0..%1").arg(INT_MAX));
        if (i > 0 && atMs < previousAtMs)
            return fail(QStringLiteral("'atMs' must be nondecreasing"));
        previousAtMs = atMs;

        QString windowName;
        if (o.contains(QLatin1String("window"))) {
            if (!o.value(QLatin1String("window")).isString())
                return failAt(atMs, QStringLiteral("'window' must be a string"));
            windowName = o.value(QLatin1String("window")).toString();
            if (!isOmaWindowName(windowName))
                return failAt(atMs, QStringLiteral("unknown window"));
        }
        QSet<QString> allowed = {QStringLiteral("type"), QStringLiteral("atMs")};
        if (isKey)
            allowed.unite({QStringLiteral("key"), QStringLiteral("window")});
        else if (isClick)
            allowed.unite({QStringLiteral("window"), QStringLiteral("button"),
                           QStringLiteral("target"), QStringLiteral("x"),
                           QStringLiteral("y")});
        else
            allowed.unite({QStringLiteral("window"), QStringLiteral("filename")});
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!allowed.contains(it.key())) {
                return failAt(atMs,
                              QStringLiteral("unknown field '%1'").arg(it.key()));
            }
        }

        Action a;
        a.index = i;
        a.atMs = atMs;
        a.type = type;
        a.windowName = windowName;

        if (isKey) {
            if (!o.contains(QLatin1String("key")))
                return failAt(atMs, QStringLiteral("missing key"));
            if (!o.value(QLatin1String("key")).isString())
                return failAt(atMs, QStringLiteral("key must be a string"));
            const QString keyName = o.value(QLatin1String("key")).toString();
            Qt::Key code = Qt::Key_unknown;
            bool isMod = false;
            QChar base;
            bool single = false;
            QString detail;
            if (!parseKey(keyName, &code, &isMod, &base, &single, &detail)) {
                return failAt(atMs,
                              QStringLiteral("invalid key '%1': %2").arg(keyName).arg(detail));
            }
            const int ic = static_cast<int>(code);
            if (type == QLatin1String("key_press")) {
                if (heldCheck.contains(ic))
                    return failAt(atMs, QStringLiteral("duplicate key press without release"));
                heldCheck.insert(ic);
            } else {
                if (!heldCheck.contains(ic))
                    return failAt(atMs, QStringLiteral("key release without matching press"));
                heldCheck.remove(ic);
            }
            a.keyName = keyName;
            a.keyCode = code;
            a.baseChar = base;
            a.isSingleChar = single;
            a.isModifier = isMod;
        } else if (isClick) {
            if (o.contains(QLatin1String("button"))) {
                if (!o.value(QLatin1String("button")).isString())
                    return failAt(atMs, QStringLiteral("'button' must be a string"));
                const QString b = o.value(QLatin1String("button")).toString().toLower();
                if (b == QLatin1String("left"))
                    a.button = Qt::LeftButton;
                else if (b == QLatin1String("middle"))
                    a.button = Qt::MiddleButton;
                else if (b == QLatin1String("right"))
                    a.button = Qt::RightButton;
                else
                    return failAt(atMs, QStringLiteral("invalid button"));
            }
            const bool hasTarget = o.contains(QLatin1String("target"));
            const bool hasX = o.contains(QLatin1String("x"));
            const bool hasY = o.contains(QLatin1String("y"));
            if (hasTarget && (hasX || hasY))
                return failAt(atMs, QStringLiteral("'target' cannot be combined with coordinate"));
            if (!hasTarget && !(hasX && hasY))
                return failAt(atMs, QStringLiteral("exactly one of target or x/y coordinate required"));
            if (hasTarget) {
                if (!o.value(QLatin1String("target")).isString())
                    return failAt(atMs, QStringLiteral("target must be a string"));
                const QString target = o.value(QLatin1String("target")).toString();
                if (target.isEmpty())
                    return failAt(atMs, QStringLiteral("target must be nonempty"));
                a.hasTarget = true;
                a.target = target;
            } else {
                if (!o.value(QLatin1String("x")).isDouble()
                    || !o.value(QLatin1String("y")).isDouble())
                    return failAt(atMs, QStringLiteral("coordinate x and y must be numbers"));
                const double x = o.value(QLatin1String("x")).toDouble();
                const double y = o.value(QLatin1String("y")).toDouble();
                if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0)
                    return failAt(atMs, QStringLiteral("coordinate x and y must be finite numbers >= 0"));
                a.x = x;
                a.y = y;
            }
        } else {
            if (!o.contains(QLatin1String("filename")))
                return failAt(atMs, QStringLiteral("missing filename"));
            if (!o.value(QLatin1String("filename")).isString())
                return failAt(atMs, QStringLiteral("filename must be a string"));
            const QString filename = o.value(QLatin1String("filename")).toString();
            if (filename.isEmpty())
                return failAt(atMs, QStringLiteral("filename must be nonempty"));
            if (filename.contains(QLatin1Char('/')) || filename.contains(QLatin1Char('\\')))
                return failAt(atMs, QStringLiteral("filename must be a basename"));
            if (filename.contains(QChar(0)))
                return failAt(atMs, QStringLiteral("filename contains NUL"));
            bool badControl = false;
            for (int ci = 0; ci < filename.size(); ++ci) {
                const uint cu = filename.at(ci).unicode();
                if (cu < 0x20 || cu == 0x7F)
                    badControl = true;
            }
            if (badControl)
                return failAt(atMs, QStringLiteral("filename contains control characters"));
            if (filename == QLatin1String(".") || filename == QLatin1String("..")
                || filename.startsWith(QLatin1Char('.'))
                || filename.contains(QLatin1String("..")))
                return failAt(atMs, QStringLiteral("invalid dot/traversal filename"));
            if (!filename.toLower().endsWith(QLatin1String(".png")))
                return failAt(atMs, QStringLiteral("filename must end with .png"));
            if (screenshotNames.contains(filename))
                return failAt(atMs, QStringLiteral("duplicate filename"));
            screenshotNames.insert(filename);
            a.filename = filename;
        }
        actions.append(a);
    }

    m_actions = actions;
    m_hasSequence = true;
    return true;
}

void AutomationRunner::start()
{
    if (m_started || m_finished)
        return;
    m_started = true;
    if (!m_hasSequence) {
        m_finished = true;
        emit finished(false, QStringLiteral("no sequence loaded"));
        return;
    }
    m_startupClock.start();
    onReadyPoll();
    if (!m_finished && !m_clockRunning)
        m_readyTimer.start();
}
