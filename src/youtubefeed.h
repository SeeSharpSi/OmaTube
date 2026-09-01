#pragma once

#include "domain.h"

#include <QList>
#include <QUrl>

#include <optional>

struct YouTubeFeed
{
    QList<Video> videos;
    QString channelId;
    QString channelTitle;
};

[[nodiscard]] QUrl longFormYouTubeFeedUrl(const QString &channelId);

[[nodiscard]] std::optional<YouTubeFeed> parseYouTubeFeed(
    const QByteArray &xml,
    QString *error = nullptr);
