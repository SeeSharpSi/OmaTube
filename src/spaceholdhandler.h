#pragma once

#include <QObject>
#include <QTimer>

class SpaceHoldHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)

public:
    explicit SpaceHoldHandler(QObject *parent = nullptr);
    ~SpaceHoldHandler() override;

    bool held() const { return m_held; }

signals:
    void heldChanged();
    void tapped();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onHoldTimeout();
    void onApplicationStateChanged(Qt::ApplicationState state);

private:
    void cancelPendingWithoutTap();
    void resetHeld();
    void handleDeactivation();

    QTimer *m_timer = nullptr;
    bool m_pending = false;
    bool m_held = false;
};
