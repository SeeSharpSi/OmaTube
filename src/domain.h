#pragma once

#include <QDateTime>
#include <QString>
#include <QMetaType>

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

    bool operator==(const Video &) const = default;
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
