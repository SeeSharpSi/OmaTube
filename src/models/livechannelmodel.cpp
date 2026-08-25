#include "models/livechannelmodel.h"

#include <QUrl>

LiveChannelModel::LiveChannelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LiveChannelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_channels.size();
}

QVariant LiveChannelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_channels.size())
        return {};

    const LiveChannel &channel = m_channels.at(index.row());
    switch (role) {
    case ChannelIdRole:
        return channel.channelId;
    case ChannelTitleRole:
        return channel.channelTitle;
    case AvatarUrlRole:
        return channel.avatarUrl;
    case VideoIdRole:
        return channel.videoId;
    case VideoTitleRole:
        return channel.videoTitle;
    case VideoUrlRole:
        return QUrl(QStringLiteral("https://www.youtube.com/watch?v=%1").arg(channel.videoId));
    default:
        return {};
    }
}

QHash<int, QByteArray> LiveChannelModel::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"},
        {ChannelTitleRole, "channelTitle"},
        {AvatarUrlRole, "avatarUrl"},
        {VideoIdRole, "videoId"},
        {VideoTitleRole, "videoTitle"},
        {VideoUrlRole, "videoUrl"},
    };
}

void LiveChannelModel::setLiveChannels(QList<LiveChannel> channels)
{
    beginResetModel();
    m_channels = std::move(channels);
    endResetModel();
}
