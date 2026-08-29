#include "spaceholdhandler.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>

SpaceHoldHandler::SpaceHoldHandler(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &SpaceHoldHandler::onHoldTimeout);

    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);

    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged,
                this, &SpaceHoldHandler::onApplicationStateChanged);
    }
}

SpaceHoldHandler::~SpaceHoldHandler()
{
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removeEventFilter(this);
}

void SpaceHoldHandler::onHoldTimeout()
{
    if (!m_pending)
        return;
    m_pending = false;
    if (m_held)
        return;
    m_held = true;
    emit heldChanged();
}

void SpaceHoldHandler::onApplicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive)
        handleDeactivation();
}

void SpaceHoldHandler::cancelPendingWithoutTap()
{
    if (m_pending) {
        m_timer->stop();
        m_pending = false;
    }
}

void SpaceHoldHandler::resetHeld()
{
    if (m_held) {
        m_held = false;
        emit heldChanged();
    }
}

void SpaceHoldHandler::handleDeactivation()
{
    cancelPendingWithoutTap();
    resetHeld();
}

bool SpaceHoldHandler::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (!event)
        return false;

    const QEvent::Type type = event->type();

    if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() != Qt::Key_Space)
            return false;
        if (ke->isAutoRepeat()) {
            // Consume autorepeat but do not change state.
            return true;
        }
        if (type == QEvent::KeyPress) {
            if (m_pending || m_held) {
                return true;
            }
            m_pending = true;
            m_timer->start();
            return true;
        }
        // KeyRelease non-autorepeat
        if (m_held) {
            m_timer->stop();
            m_pending = false;
            m_held = false;
            emit heldChanged();
            return true;
        }
        if (m_pending) {
            m_timer->stop();
            m_pending = false;
            emit tapped();
            return true;
        }
        return true;
    }

    if (type == QEvent::ApplicationDeactivate || type == QEvent::WindowDeactivate) {
        handleDeactivation();
        return false;
    }

    if (type == QEvent::ApplicationStateChange) {
        if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            if (gui->applicationState() != Qt::ApplicationActive)
                handleDeactivation();
        }
        return false;
    }

    return false;
}
