#include "models/feedmodel.h"

#include <QSet>
#include <QUrl>

FeedModel::FeedModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FeedModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_videos.size();
}

QVariant FeedModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_videos.size())
        return {};

    const Video &video = m_videos.at(index.row());
    switch (role) {
    case VideoIdRole:
        return video.id;
    case ChannelIdRole:
        return video.channelId;
    case ChannelTitleRole:
        return video.channelTitle;
    case TitleRole:
        return video.title;
    case PublishedAtRole:
        return video.publishedAt;
    case VideoUrlRole:
        return QUrl(QStringLiteral("https://www.youtube.com/watch?v=%1").arg(video.id));
    case WatchProgressPercentRole:
        return video.watchProgressPercent;
    default:
        return {};
    }
}

QHash<int, QByteArray> FeedModel::roleNames() const
{
    return {
        {VideoIdRole, "videoId"},
        {ChannelIdRole, "channelId"},
        {ChannelTitleRole, "channelTitle"},
        {TitleRole, "title"},
        {PublishedAtRole, "publishedAt"},
        {VideoUrlRole, "videoUrl"},
        {WatchProgressPercentRole, "watchProgressPercent"},
    };
}

void FeedModel::setVideos(QList<Video> videos)
{
    beginResetModel();
    m_videos = std::move(videos);
    endResetModel();
}

void FeedModel::updateVideos(QList<Video> videos)
{
    QSet<QString> targetIds;
    targetIds.reserve(videos.size());
    for (const Video &video : videos)
        targetIds.insert(video.id);

    for (int row = m_videos.size() - 1; row >= 0; --row) {
        if (!targetIds.contains(m_videos.at(row).id)) {
            beginRemoveRows({}, row, row);
            m_videos.removeAt(row);
            endRemoveRows();
        }
    }

    auto changedRoles = [](const Video &oldVideo, const Video &newVideo) {
        QVector<int> roles;
        if (oldVideo.channelId != newVideo.channelId)
            roles << ChannelIdRole;
        if (oldVideo.channelTitle != newVideo.channelTitle)
            roles << ChannelTitleRole;
        if (oldVideo.title != newVideo.title)
            roles << TitleRole;
        if (oldVideo.publishedAt != newVideo.publishedAt)
            roles << PublishedAtRole;
        if (oldVideo.watchProgressPercent != newVideo.watchProgressPercent)
            roles << WatchProgressPercentRole;
        return roles;
    };

    for (int targetPos = 0; targetPos < videos.size(); ++targetPos) {
        const QString targetId = videos.at(targetPos).id;
        if (targetPos >= m_videos.size() || m_videos.at(targetPos).id != targetId) {
            int currentAt = -1;
            for (int row = targetPos + 1; row < m_videos.size(); ++row) {
                if (m_videos.at(row).id == targetId) {
                    currentAt = row;
                    break;
                }
            }
            if (currentAt == -1) {
                beginInsertRows({}, targetPos, targetPos);
                m_videos.insert(targetPos, videos.at(targetPos));
                endInsertRows();
                continue;
            }
            beginMoveRows({}, currentAt, currentAt, {}, targetPos);
            m_videos.move(currentAt, targetPos);
            endMoveRows();
        }

        const QVector<int> roles = changedRoles(m_videos.at(targetPos), videos.at(targetPos));
        if (m_videos.at(targetPos) != videos.at(targetPos)) {
            m_videos[targetPos] = videos.at(targetPos);
            if (!roles.isEmpty())
                emit dataChanged(index(targetPos), index(targetPos), roles);
        }
    }
}

void FeedModel::appendVideos(const QList<Video> &videos)
{
    if (videos.isEmpty())
        return;
    beginInsertRows({}, m_videos.size(), m_videos.size() + videos.size() - 1);
    m_videos.append(videos);
    endInsertRows();
}
