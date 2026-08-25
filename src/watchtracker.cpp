#include "watchtracker.h"

#include <QtGlobal>

#include <cmath>

namespace {
// Reports arrive roughly every five seconds; deltas beyond this look like
// seeks or missed-report gaps and are not credited.
constexpr double maximumCreditedDeltaSeconds = 15.0;
// Sessions shorter than this do not bump the watch count.
constexpr qint64 minimumCountedSessionSeconds = 5;
}

void WatchTracker::setActiveVideo(const QString &videoId, int initialPositionSeconds)
{
    if (m_activeVideoId == videoId)
        return;
    finishActiveSession();
    m_activeVideoId = videoId;
    m_hasBasePosition = false;
    m_basePosition = 0.0;
    m_furthestPositionSeconds = qMax(0, initialPositionSeconds);
    m_furthestObservedThisSession = 0;
    m_sessionAccruedSeconds = 0;
    m_pendingSecondsDelta = 0;
    m_sessionCounted = false;
    m_countPending = false;
}

void WatchTracker::clearActiveVideo()
{
    finishActiveSession();
    m_activeVideoId.clear();
    m_hasBasePosition = false;
    m_basePosition = 0.0;
}

void WatchTracker::reportPlayback(const QString &videoId, double positionSeconds, bool playing)
{
    if (videoId.isEmpty() || videoId != m_activeVideoId || !std::isfinite(positionSeconds)
        || positionSeconds < 0.0)
        return;

    const int positionSecondsInt = static_cast<int>(positionSeconds);
    if (positionSecondsInt > m_furthestPositionSeconds)
        m_furthestPositionSeconds = positionSecondsInt;

    if (!m_hasBasePosition) {
        m_hasBasePosition = true;
        m_basePosition = positionSeconds;
        return;
    }

    const double delta = positionSeconds - m_basePosition;
    m_basePosition = positionSeconds;
    if (!playing || delta <= 0.0 || delta > maximumCreditedDeltaSeconds)
        return;

    const qint64 creditedSeconds = static_cast<qint64>(std::llround(delta));
    if (creditedSeconds <= 0)
        return;

    m_furthestObservedThisSession =
        qMax(m_furthestObservedThisSession, positionSecondsInt);
    m_pendingSecondsDelta += creditedSeconds;
    m_sessionAccruedSeconds += creditedSeconds;
    if (!m_sessionCounted && m_sessionAccruedSeconds >= minimumCountedSessionSeconds) {
        m_sessionCounted = true;
        m_countPending = true;
    }
}

QList<WatchProgressUpdate> WatchTracker::takePendingUpdates()
{
    QList<WatchProgressUpdate> updates = std::move(m_pending);
    m_pending.clear();

    const bool hasActiveUpdate =
        !m_activeVideoId.isEmpty()
        && (m_pendingSecondsDelta > 0 || m_countPending || m_furthestObservedThisSession > 0);
    if (hasActiveUpdate) {
        updates.append({m_activeVideoId, m_pendingSecondsDelta, m_furthestPositionSeconds,
                        m_countPending});
        m_pendingSecondsDelta = 0;
        m_countPending = false;
        m_furthestObservedThisSession = 0;
    }
    return updates;
}

void WatchTracker::finishActiveSession()
{
    if (m_activeVideoId.isEmpty())
        return;
    if (m_pendingSecondsDelta > 0 || m_countPending || m_furthestObservedThisSession > 0) {
        m_pending.append(
            {m_activeVideoId, m_pendingSecondsDelta, m_furthestPositionSeconds, m_countPending});
        m_pendingSecondsDelta = 0;
        m_countPending = false;
        m_furthestObservedThisSession = 0;
    }
}
