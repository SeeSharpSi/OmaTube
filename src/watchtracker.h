#pragma once

#include <QList>
#include <QString>

struct WatchProgressUpdate
{
    QString videoId;
    qint64 watchedSecondsDelta = 0;
    int lastPositionSeconds = 0;
    bool countSession = false;

    bool operator==(const WatchProgressUpdate &) const = default;
};

// Accumulates playback time from player-reported positions. Time is credited
// only between consecutive playing reports whose position delta looks like
// continuous playback; seeks and report gaps resynchronize without credit.
class WatchTracker
{
public:
    // Starts a new session for videoId. Any pending data for the previous
    // session is kept until takePendingUpdates(). initialPositionSeconds
    // seeds the furthest-known position (usually the stored resume point).
    void setActiveVideo(const QString &videoId, int initialPositionSeconds = 0);
    void clearActiveVideo();
    void reportPlayback(const QString &videoId, double positionSeconds, bool playing);
    [[nodiscard]] QList<WatchProgressUpdate> takePendingUpdates();

private:
    void finishActiveSession();

    QString m_activeVideoId;
    bool m_hasBasePosition = false;
    double m_basePosition = 0.0;
    int m_furthestPositionSeconds = 0;
    int m_furthestObservedThisSession = 0;
    qint64 m_sessionAccruedSeconds = 0;
    qint64 m_pendingSecondsDelta = 0;
    bool m_sessionCounted = false;
    bool m_countPending = false;
    QList<WatchProgressUpdate> m_pending;
};
