#pragma once

#include "domain.h"

#include <QAbstractListModel>

class FeedModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        VideoIdRole = Qt::UserRole + 1,
        ChannelIdRole,
        ChannelTitleRole,
        TitleRole,
        PublishedAtRole,
        VideoUrlRole,
        WatchProgressPercentRole,
    };
    Q_ENUM(Role)

    explicit FeedModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setVideos(QList<Video> videos);

private:
    QList<Video> m_videos;
};
