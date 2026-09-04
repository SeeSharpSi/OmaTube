#include "automationfixture.h"

#include "repository.h"

#include <QDateTime>

namespace {
QDateTime fixedDateTime(const QString &iso, int offsetSecs = 0)
{
    QDateTime base = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!base.isValid())
        base = QDateTime::fromString(QStringLiteral("2026-01-01T12:00:00.000Z"), Qt::ISODateWithMs);
    base = base.toUTC();
    return offsetSecs == 0 ? base : base.addSecs(offsetSecs);
}

Channel makeAutomationChannel(const QString &id, const QString &title, const QString &uploadsId)
{
    Channel channel;
    channel.id = id;
    channel.originalInput = id;
    channel.handle = QStringLiteral("@automation");
    channel.title = title;
    channel.avatarUrl = {};
    channel.uploadsPlaylistId = uploadsId;
    channel.metadataFetchedAt = fixedDateTime(QStringLiteral("2026-01-01T12:00:00.000Z"));
    return channel;
}

Video makeAutomationVideo(
    const QString &id,
    const QString &channelId,
    const QString &title,
    const QDateTime &publishedAt)
{
    Video video;
    video.id = id;
    video.channelId = channelId;
    video.title = title;
    video.publishedAt = publishedAt;
    video.isBroadcast = false;
    video.broadcastState = QStringLiteral("none");
    video.fetchedAt = fixedDateTime(QStringLiteral("2026-01-01T12:00:00.000Z"));
    video.durationSeconds = 600;
    return video;
}
}

bool AutomationFixture::seed(const QString &databasePath, QString *error)
{
    if (databasePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Automation fixture needs a database path.");
        return false;
    }

    Repository repository(databasePath);
    if (!repository.open(error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Could not open automation database.");
        return false;
    }

    const qint64 musicId = repository.addCategory(QStringLiteral("Automation Music"), error);
    if (musicId < 0)
        return false;
    const qint64 techId = repository.addCategory(QStringLiteral("Automation Tech"), error);
    if (techId < 0)
        return false;

    const Channel first = makeAutomationChannel(
        QStringLiteral("UCautomation01"),
        QStringLiteral("Automation Channel One"),
        QStringLiteral("UUautomation01"));
    const Channel second = makeAutomationChannel(
        QStringLiteral("UCautomation02"),
        QStringLiteral("Automation Channel Two"),
        QStringLiteral("UUautomation02"));
    if (!repository.upsertChannel(first, error))
        return false;
    if (!repository.upsertChannel(second, error))
        return false;
    if (!repository.setChannelCategories(first.id, {musicId}, error))
        return false;
    if (!repository.setChannelCategories(second.id, {techId}, error))
        return false;

    const QDateTime base = fixedDateTime(QStringLiteral("2026-01-01T12:00:00.000Z"));
    const QList<Video> videos{
        makeAutomationVideo(QStringLiteral("AUTO0000001"), first.id, QStringLiteral("Automation Video 1"), base),
        makeAutomationVideo(QStringLiteral("AUTO0000002"), first.id, QStringLiteral("Automation Video 2"), base.addSecs(-60)),
        makeAutomationVideo(QStringLiteral("AUTO0000003"), first.id, QStringLiteral("Automation Video 3"), base.addSecs(-120)),
        makeAutomationVideo(QStringLiteral("AUTO0000004"), second.id, QStringLiteral("Automation Video 4"), base.addSecs(-180)),
        makeAutomationVideo(QStringLiteral("AUTO0000005"), second.id, QStringLiteral("Automation Video 5"), base.addSecs(-240)),
    };
    if (!repository.upsertVideos(videos, error))
        return false;

    if (!repository.setChannelHistoryState(first.id, QString(), true, error))
        return false;
    if (!repository.setChannelHistoryState(second.id, QString(), true, error))
        return false;

    if (!repository.applyWatchProgress(QStringLiteral("AUTO0000001"), 120, 120, true, error))
        return false;

    return true;
}
