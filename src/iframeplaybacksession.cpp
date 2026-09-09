#include "iframeplaybacksession.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <cmath>
#include <limits>

IframePlaybackSession::IframePlaybackSession(QObject *parent)
    : QObject(parent)
{
}

QString IframePlaybackSession::begin(const QString &videoId)
{
    if (videoId.isEmpty()) {
        stop();
        return {};
    }
    m_videoId = videoId;
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_token;
}

void IframePlaybackSession::stop()
{
    m_videoId.clear();
    m_token.clear();
}

void IframePlaybackSession::acceptReport(const QString &json)
{
    if (m_token.isEmpty())
        return;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return;
    const QJsonObject payload = document.object();
    const QJsonValue idValue = payload.value(QStringLiteral("videoId"));
    const QJsonValue sessionValue = payload.value(QStringLiteral("loadSession"));
    const QJsonValue stateValue = payload.value(QStringLiteral("state"));
    const QJsonValue timeValue = payload.value(QStringLiteral("time"));
    if (!idValue.isString() || !sessionValue.isString()
        || idValue.toString() != m_videoId || sessionValue.toString() != m_token
        || !timeValue.isDouble() || !stateValue.isDouble())
        return;
    const double time = timeValue.toDouble();
    const double state = stateValue.toDouble();
    // WatchTracker stores positions as integer seconds.
    if (!std::isfinite(time) || time < 0.0 || time > std::numeric_limits<int>::max())
        return;
    if (state != -1 && state != 0 && state != 1 && state != 2 && state != 3 && state != 5)
        return;
    emit playbackUpdated(time, state == 1);
}
