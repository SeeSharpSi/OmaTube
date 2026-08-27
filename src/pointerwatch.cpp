#include "pointerwatch.h"

#include <QCursor>
#include <QRectF>
#include <QSizeF>
#include <QWindow>

namespace {

QCursor cursorForHiddenState(bool hidden)
{
    return hidden ? QCursor(Qt::BlankCursor) : QCursor(Qt::ArrowCursor);
}

} // namespace

PointerWatch::PointerWatch(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(pollIntervalMs);
    connect(&m_poll, &QTimer::timeout, this, &PointerWatch::poll);
}

void PointerWatch::watch(QObject *window)
{
    stop();
    auto *nativeWindow = qobject_cast<QWindow *>(window);
    if (!nativeWindow)
        return;
    m_window = nativeWindow;
    m_lastPos = QCursor::pos();
    m_hidden = false;
    m_idle.start();
    m_poll.start();
}

void PointerWatch::stop()
{
    const bool wasHidden = m_hidden;
    m_poll.stop();
    if (m_hidden && m_window)
        m_window->unsetCursor();
    m_hidden = false;
    m_window.clear();
    if (wasHidden)
        emit hiddenChanged();
}

void PointerWatch::poll()
{
    const QPointF pos = QCursor::pos();
    if (!m_window || !m_window->isVisible()) {
        m_lastPos = pos;
        return;
    }
    if (pos != m_lastPos) {
        m_lastPos = pos;
        if (!cursorOverWindow()) {
            setHidden(true);
            return;
        }
        m_idle.restart();
        if (m_hidden)
            setHidden(false);
        return;
    }
    if (!cursorOverWindow()) {
        setHidden(true);
        return;
    }
    if (m_hidden || !m_window->isActive())
        return;
    if (m_idle.elapsed() >= idleTimeoutMs)
        setHidden(true);
}

bool PointerWatch::cursorOverWindow() const
{
    const QRectF bounds(QPointF(0, 0), QSizeF(m_window->size()));
    return bounds.contains(m_window->mapFromGlobal(m_lastPos));
}

void PointerWatch::setHidden(bool hidden)
{
    if (!m_window || m_hidden == hidden)
        return;
    if (hidden)
        m_window->setCursor(cursorForHiddenState(true));
    else
        m_window->unsetCursor();
    m_hidden = hidden;
    emit hiddenChanged();
}

bool PointerWatch::hidden() const noexcept
{
    return m_hidden;
}
