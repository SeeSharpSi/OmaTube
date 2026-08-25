#pragma once

#include <QQuickItem>
#include <QPointer>
#include <QString>

class QQuickWindow;

class MacVideoPlayerNative final : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString videoId READ videoId WRITE setVideoId NOTIFY videoIdChanged)

public:
    explicit MacVideoPlayerNative(QQuickItem *parent = nullptr);
    ~MacVideoPlayerNative() override;

    [[nodiscard]] QString videoId() const;
    void setVideoId(const QString &videoId);
    Q_INVOKABLE void stop();

signals:
    void videoIdChanged();

private:
    void setWindow(QQuickWindow *window);
    void syncNativeView();
    void loadVideo();

    QString m_videoId;
    QPointer<QQuickWindow> m_window;
    void *m_webView = nullptr;
    void *m_navigationDelegate = nullptr;
};
