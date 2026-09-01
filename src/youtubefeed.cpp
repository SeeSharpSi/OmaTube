#include "youtubefeed.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <utility>

namespace {

bool isValidChannelId(const QString &channelId)
{
    static const QRegularExpression expression(QStringLiteral("^UC[A-Za-z0-9_-]{22}$"));
    return expression.match(channelId).hasMatch();
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

struct EntryFields
{
    QString videoId;
    QString channelId;
    QString title;
    QString published;
};

EntryFields parseEntry(QXmlStreamReader &reader)
{
    EntryFields entry;
    while (reader.readNextStartElement()) {
        if (reader.name() == u"videoId")
            entry.videoId = reader.readElementText(QXmlStreamReader::SkipChildElements);
        else if (reader.name() == u"channelId")
            entry.channelId = reader.readElementText(QXmlStreamReader::SkipChildElements);
        else if (reader.name() == u"title")
            entry.title = reader.readElementText(QXmlStreamReader::SkipChildElements);
        else if (reader.name() == u"published")
            entry.published = reader.readElementText(QXmlStreamReader::SkipChildElements);
        else
            reader.skipCurrentElement();
    }
    return entry;
}
}

QUrl longFormYouTubeFeedUrl(const QString &channelId)
{
    if (!isValidChannelId(channelId))
        return {};
    QUrl url(QStringLiteral("https://www.youtube.com/feeds/videos.xml"));
    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("playlist_id"),
        QStringLiteral("UULF%1").arg(channelId.mid(2)));
    url.setQuery(query);
    return url;
}

std::optional<YouTubeFeed> parseYouTubeFeed(const QByteArray &xml, QString *error)
{
    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement() || reader.name() != u"feed") {
        setError(error, QStringLiteral("Response is not an Atom feed document."));
        return std::nullopt;
    }

    QList<EntryFields> entries;
    QString feedTitle;
    QString authorName;
    QString feedChannelId;
    while (reader.readNextStartElement()) {
        if (reader.name() == u"entry") {
            entries.append(parseEntry(reader));
        } else if (reader.name() == u"title") {
            feedTitle = reader.readElementText(QXmlStreamReader::SkipChildElements);
        } else if (reader.name() == u"author") {
            while (reader.readNextStartElement()) {
                if (reader.name() == u"name")
                    authorName = reader.readElementText(QXmlStreamReader::SkipChildElements);
                else
                    reader.skipCurrentElement();
            }
        } else if (reader.name() == u"channelId") {
            feedChannelId = reader.readElementText(QXmlStreamReader::SkipChildElements);
        } else {
            reader.skipCurrentElement();
        }
    }
    if (reader.hasError()) {
        setError(error, QStringLiteral("Invalid Atom feed: %1").arg(reader.errorString()));
        return std::nullopt;
    }

    const QString channelTitle = !authorName.isEmpty() ? authorName : feedTitle;
    if (feedChannelId.isEmpty() || channelTitle.isEmpty()) {
        setError(error, QStringLiteral("Atom feed is missing channel identity or title."));
        return std::nullopt;
    }

    YouTubeFeed feed;
    feed.channelId = feedChannelId;
    feed.channelTitle = channelTitle;
    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    for (const EntryFields &entry : entries) {
        const QDateTime publishedAt = QDateTime::fromString(entry.published, Qt::ISODate);
        if (entry.videoId.isEmpty() || entry.channelId.isEmpty() || entry.title.isEmpty()
            || !publishedAt.isValid())
            continue;
        feed.videos.append(Video{
            entry.videoId,
            entry.channelId,
            channelTitle,
            entry.title,
            publishedAt.toUTC(),
            false,
            QStringLiteral("none"),
            fetchedAt,
            -1,
        });
    }
    return feed;
}
