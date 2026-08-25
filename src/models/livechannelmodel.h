#pragma once

#include "domain.h"

#include <QAbstractListModel>

class LiveChannelModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ChannelIdRole = Qt::UserRole + 1,
        ChannelTitleRole,
        AvatarUrlRole,
        VideoIdRole,
        VideoTitleRole,
        VideoUrlRole,
    };
    Q_ENUM(Role)

    explicit LiveChannelModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setLiveChannels(QList<LiveChannel> channels);

private:
    QList<LiveChannel> m_channels;
};
