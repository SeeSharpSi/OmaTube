#include "models/channelmodel.h"

ChannelModel::ChannelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChannelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_channels.size();
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_channels.size())
        return {};

    const Channel &channel = m_channels.at(index.row());
    switch (role) {
    case ChannelIdRole:
        return channel.id;
    case TitleRole:
        return channel.title;
    case HandleRole:
        return channel.handle;
    case AvatarUrlRole:
        return channel.avatarUrl;
    case CategoryIdsRole: {
        QVariantList result;
        for (const qint64 categoryId : m_categoryIds.value(channel.id))
            result.append(categoryId);
        return result;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> ChannelModel::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"},
        {TitleRole, "title"},
        {HandleRole, "handle"},
        {AvatarUrlRole, "avatarUrl"},
        {CategoryIdsRole, "categoryIds"},
    };
}

void ChannelModel::setChannels(
    QList<Channel> channels,
    QHash<QString, QList<qint64>> categoryIds)
{
    beginResetModel();
    m_channels = std::move(channels);
    m_categoryIds = std::move(categoryIds);
    endResetModel();
}
