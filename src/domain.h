#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct Category
{
    qint64 id = -1;
    QString name;

    bool operator==(const Category &) const = default;
};

struct Channel
{
    QString id;
    QString originalInput;
    QString handle;
    QString title;
    QString avatarUrl;
    QString uploadsPlaylistId;
    QDateTime metadataFetchedAt;

    bool operator==(const Channel &) const = default;
};

struct Video
{
    QString id;
    QString channelId;
    QString channelTitle;
    QString title;
    QDateTime publishedAt;
    bool isBroadcast = false;
    QString broadcastState;
    QDateTime fetchedAt;
    int durationSeconds = -1;
    // Percentage (0-100) of the video's duration the viewer reached.
    // Negative when there is no watch progress or the duration is unknown.
    // Populated only by feed queries; not persisted.
    int watchProgressPercent = -1;

    bool operator==(const Video &) const = default;
};

// One page of a channel's uploads playlist. An empty nextPageToken means
// the playlist has no further pages.
struct UploadPage
{
    QStringList videoIds;
    QString nextPageToken;
    // Sources such as Atom feeds already provide usable video metadata and
    // can skip a separate details request.
    QList<Video> videos;
};

// How far a channel's uploads history has been fetched and where to resume.
struct ChannelHistoryState
{
    QString channelId;
    QString nextPageToken;
    bool historyComplete = false;

    bool operator==(const ChannelHistoryState &) const = default;
};

struct WatchStats
{
    QString videoId;
    qint64 watchedSeconds = 0;
    int lastPositionSeconds = 0;
    QDateTime lastWatchedAt;
    int watchCount = 0;

    bool operator==(const WatchStats &) const = default;
};

// One row of a prior playback session, joined to the video and channel
// metadata that existed when history was recorded.
struct HistoryEntry
{
    QString videoId;
    QString channelId;
    QString channelTitle;
    QString title;
    QDateTime watchedAt;
    // Percentage (0-100) the viewer reached on the most recent session.
    // Negative when there is no watch progress or the duration is unknown.
    int watchProgressPercent = -1;

    bool operator==(const HistoryEntry &) const = default;
};

// One row of the committed Watch Next queue, joined to the video and
// channel metadata that still exists. Rows whose video or channel has
// been removed are omitted.
struct WatchNextEntry
{
    QString videoId;
    QString channelId;
    QString channelTitle;
    QString title;
    QDateTime publishedAt;
    // Percentage (0-100) the viewer reached. Negative when unknown.
    int watchProgressPercent = -1;
    int position = 0;
    QDateTime addedAt;

    bool operator==(const WatchNextEntry &) const = default;
};

struct LiveChannel
{
    QString channelId;
    QString channelTitle;
    QString avatarUrl;
    QString videoId;
    QString videoTitle;

    bool operator==(const LiveChannel &) const = default;
};

Q_DECLARE_METATYPE(LiveChannel)
Q_DECLARE_METATYPE(QList<LiveChannel>)
