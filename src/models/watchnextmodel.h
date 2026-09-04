#pragma once

#include "domain.h"

#include <QAbstractListModel>

class WatchNextModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        VideoIdRole = Qt::UserRole + 1,
        ChannelIdRole,
        ChannelTitleRole,
        TitleRole,
        PublishedAtRole,
        WatchProgressPercentRole,
        PositionRole,
        AddedAtRole,
    };
    Q_ENUM(Role)

    explicit WatchNextModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<WatchNextEntry> entries);

private:
    QList<WatchNextEntry> m_entries;
};
