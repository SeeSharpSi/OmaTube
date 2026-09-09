#pragma once

#include <QObject>
#include <QString>

class IframePlaybackSession : public QObject
{
    Q_OBJECT

public:
    explicit IframePlaybackSession(QObject *parent = nullptr);

    Q_INVOKABLE QString begin(const QString &videoId);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void acceptReport(const QString &json);

signals:
    void playbackUpdated(double positionSeconds, bool playing);

private:
    QString m_videoId;
    QString m_token;
};
