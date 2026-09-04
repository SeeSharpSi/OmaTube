#include "models/watchnextmodel.h"

WatchNextModel::WatchNextModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WatchNextModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant WatchNextModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const WatchNextEntry &entry = m_entries.at(index.row());
    switch (role) {
    case VideoIdRole:
        return entry.videoId;
    case ChannelIdRole:
        return entry.channelId;
    case ChannelTitleRole:
        return entry.channelTitle;
    case TitleRole:
        return entry.title;
    case PublishedAtRole:
        return entry.publishedAt;
    case WatchProgressPercentRole:
        return entry.watchProgressPercent;
    case PositionRole:
        return entry.position;
    case AddedAtRole:
        return entry.addedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> WatchNextModel::roleNames() const
{
    return {
        {VideoIdRole, "videoId"},
        {ChannelIdRole, "channelId"},
        {ChannelTitleRole, "channelTitle"},
        {TitleRole, "title"},
        {PublishedAtRole, "publishedAt"},
        {WatchProgressPercentRole, "watchProgressPercent"},
        {PositionRole, "position"},
        {AddedAtRole, "addedAt"},
    };
}

void WatchNextModel::setEntries(QList<WatchNextEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}
