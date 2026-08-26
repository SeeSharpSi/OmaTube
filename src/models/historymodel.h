#pragma once

#include "domain.h"

#include <QAbstractListModel>

class HistoryModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        VideoIdRole = Qt::UserRole + 1,
        ChannelIdRole,
        ChannelTitleRole,
        TitleRole,
        WatchedAtRole,
        WatchProgressPercentRole,
    };
    Q_ENUM(Role)

    explicit HistoryModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<HistoryEntry> entries);

private:
    QList<HistoryEntry> m_entries;
};
