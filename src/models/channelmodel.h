#pragma once

#include "domain.h"

#include <QAbstractListModel>
#include <QHash>

class ChannelModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ChannelIdRole = Qt::UserRole + 1,
        TitleRole,
        HandleRole,
        AvatarUrlRole,
        CategoryIdsRole,
    };
    Q_ENUM(Role)

    explicit ChannelModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setChannels(QList<Channel> channels, QHash<QString, QList<qint64>> categoryIds);

private:
    QList<Channel> m_channels;
    QHash<QString, QList<qint64>> m_categoryIds;
};
