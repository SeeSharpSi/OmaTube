#pragma once

#include <QQuickItem>
#include <QPointer>
#include <QString>

class QQuickWindow;

class MacVideoPlayerNative final : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString videoId READ videoId WRITE setVideoId NOTIFY videoIdChanged)
    Q_PROPERTY(int startSeconds READ startSeconds WRITE setStartSeconds)

public:
    explicit MacVideoPlayerNative(QQuickItem *parent = nullptr);
    ~MacVideoPlayerNative() override;

    [[nodiscard]] QString videoId() const;
    void setVideoId(const QString &videoId);
    [[nodiscard]] int startSeconds() const;
    void setStartSeconds(int startSeconds);
    Q_INVOKABLE void stop();

signals:
    void videoIdChanged();
    void playbackUpdated(double positionSeconds, bool playing);

private:
    void setWindow(QQuickWindow *window);
    void syncNativeView();
    void loadVideo();

    QString m_videoId;
    int m_startSeconds = 0;
    QPointer<QQuickWindow> m_window;
    void *m_webView = nullptr;
    void *m_navigationDelegate = nullptr;
    void *m_messageHandler = nullptr;
};
