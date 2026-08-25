#include "models/feedmodel.h"

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
    };
}

void FeedModel::setVideos(QList<Video> videos)
{
    beginResetModel();
    m_videos = std::move(videos);
    endResetModel();
}
