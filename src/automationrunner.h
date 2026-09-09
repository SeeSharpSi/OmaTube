#ifndef AUTOMATIONRUNNER_H
#define AUTOMATIONRUNNER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWindow>

class QQuickItem;
class QQuickWindow;

class AutomationRunner : public QObject
{
    Q_OBJECT
public:
    explicit AutomationRunner(QObject *parent = nullptr);
    ~AutomationRunner() override;

    bool loadSequence(const QByteArray &json, QString *error);
    void start();
    bool isFinished() const;

signals:
    void finished(bool success, const QString &error);
    void actionExecuted(int index, qint64 elapsedMs);

private:
    struct Action {
        QString type;
        int index = 0;
        qint64 atMs = 0;
        QString windowName;
        QString keyName;
        Qt::Key keyCode = Qt::Key_unknown;
        QChar baseChar;
        bool isSingleChar = false;
        bool isModifier = false;
        bool hasTarget = false;
        QString target;
        double x = 0.0;
        double y = 0.0;
        Qt::MouseButton button = Qt::LeftButton;
        QString filename;
    };

    void onReadyPoll();
    void executeCurrent();
    void scheduleNext();
    void finishFailure(const QString &error);
    void finishSuccess();
    void releaseHeldSilently();

    bool appReady() const;
    QQuickWindow *findVisibleNamed(const QString &name, bool *ambiguous) const;
    bool settingsBlocksInput() const;
    QQuickWindow *resolveWindow(const QString &requested, QString *error,
                              bool forInput) const;

    bool runKey(const Action &a, QString *error);
    bool runClick(const Action &a, QString *error);
    bool runScreenshot(const Action &a, QString *error);
    void mouseClick(QQuickWindow *window, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods, const QPointF &local);

    Qt::KeyboardModifiers currentModifiers() const;
    QString dispatchText(const Action &a) const;
    static void collectByName(QQuickItem *parent, const QString &name,
                              QList<QQuickItem *> &out);

    QList<Action> m_actions;
    bool m_hasSequence = false;
    bool m_started = false;
    bool m_finished = false;
    QTimer m_readyTimer;
    QTimer m_stepTimer;
    QElapsedTimer m_startupClock;
    QElapsedTimer m_clock;
    bool m_clockRunning = false;
    int m_next = 0;
    QSet<int> m_held;
    QMap<int, QPointer<QWindow>> m_pressWindows;
};

#endif
