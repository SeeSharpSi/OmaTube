#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QTimer>

class QWindow;

// Hides the mouse cursor after the user stops moving it over the player
// window and restores it on any movement. It polls the global cursor
// position instead of relying on delivered hover events because WebEngine
// captures mouse input before it reaches the rest of the scene graph.
class PointerWatch final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)

public:
    explicit PointerWatch(QObject *parent = nullptr);

    Q_INVOKABLE void watch(QObject *window);
    Q_INVOKABLE void stop();

    [[nodiscard]] bool hidden() const noexcept;

signals:
    void hiddenChanged();

private slots:
    void poll();

private:
    void setHidden(bool hidden);
    [[nodiscard]] bool cursorOverWindow() const;

    static constexpr int pollIntervalMs = 150;
    static constexpr int idleTimeoutMs = 2000;

    QPointer<QWindow> m_window;
    QTimer m_poll;
    QElapsedTimer m_idle;
    QPointF m_lastPos;
    bool m_hidden = false;
};
