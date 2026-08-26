#include "models/historymodel.h"

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const HistoryEntry &entry = m_entries.at(index.row());
    switch (role) {
    case VideoIdRole:
        return entry.videoId;
    case ChannelIdRole:
        return entry.channelId;
    case ChannelTitleRole:
        return entry.channelTitle;
    case TitleRole:
        return entry.title;
    case WatchedAtRole:
        return entry.watchedAt;
    case WatchProgressPercentRole:
        return entry.watchProgressPercent;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {VideoIdRole, "videoId"},
        {ChannelIdRole, "channelId"},
        {ChannelTitleRole, "channelTitle"},
        {TitleRole, "title"},
        {WatchedAtRole, "watchedAt"},
        {WatchProgressPercentRole, "watchProgressPercent"},
    };
}

void HistoryModel::setEntries(QList<HistoryEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}
