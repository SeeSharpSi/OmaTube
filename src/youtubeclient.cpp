#include "youtubeclient.h"

#include "youtubefeed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

Q_LOGGING_CATEGORY(youTubeLog, "omatube.youtube", QtWarningMsg)

namespace {
constexpr qsizetype maximumResponseSize = 2 * 1024 * 1024;
constexpr qsizetype maximumYtDlpOutputSize = 8 * 1024 * 1024;
constexpr int maximumYtDlpProcesses = 2;
constexpr int keylessHistoryPageSize = 10;

std::optional<QJsonObject> parseObject(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("YouTube returned invalid JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    return document.object();
}

QString thumbnailUrl(const QJsonObject &snippet)
{
    const QJsonObject thumbnails = snippet.value(QStringLiteral("thumbnails")).toObject();
    for (const QString &name : {QStringLiteral("medium"), QStringLiteral("default"), QStringLiteral("high")}) {
        const QString url = thumbnails.value(name).toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            return url;
    }
    return {};
}

int durationSeconds(const QString &duration)
{
    static const QRegularExpression expression(
        QStringLiteral("^P(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?$"));
    const QRegularExpressionMatch match = expression.match(duration);
    if (!match.hasMatch())
        return -1;

    qint64 seconds = 0;
    bool hasComponent = false;
    const qint64 multipliers[] = {86400, 3600, 60, 1};
    for (int index = 1; index <= 4; ++index) {
        if (!match.captured(index).isEmpty()) {
            hasComponent = true;
            seconds += match.captured(index).toLongLong() * multipliers[index - 1];
        }
    }
    if (!hasComponent || seconds > std::numeric_limits<int>::max())
        return -1;
    return static_cast<int>(seconds);
}

QList<QJsonObject> parseJsonLines(const QByteArray &json, QString *error)
{
    if (error)
        error->clear();
    QList<QJsonObject> objects;
    for (const QByteArray &line : json.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = QStringLiteral("yt-dlp returned invalid JSON: %1")
                             .arg(parseError.errorString());
            }
            return {};
        }
        objects.append(document.object());
    }
    return objects;
}

QDateTime ytDlpPublishedAt(const QJsonObject &object)
{
    for (const QString &name : {QStringLiteral("release_timestamp"), QStringLiteral("timestamp")}) {
        const QJsonValue value = object.value(name);
        if (value.isDouble() && value.toDouble() > 0)
            return QDateTime::fromSecsSinceEpoch(
                static_cast<qint64>(value.toDouble()),
                QTimeZone::utc());
    }
    const QString uploadDate = object.value(QStringLiteral("upload_date")).toString();
    const QDate date = QDate::fromString(uploadDate, QStringLiteral("yyyyMMdd"));
    return date.isValid() ? date.startOfDay(QTimeZone::utc()) : QDateTime{};
}

QString ytDlpThumbnail(const QJsonObject &object)
{
    const QJsonArray thumbnails = object.value(QStringLiteral("thumbnails")).toArray();
    for (qsizetype index = thumbnails.size() - 1; index >= 0; --index) {
        const QString url = thumbnails.at(index).toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            return url;
    }
    return object.value(QStringLiteral("thumbnail")).toString();
}

QString uploadsPlaylistId(const QString &channelId)
{
    return channelId.startsWith(QStringLiteral("UC")) && channelId.size() == 24
        ? QStringLiteral("UU%1").arg(channelId.mid(2))
        : QString{};
}

QString channelUrlForReference(const ChannelReference &reference)
{
    return reference.kind == ChannelReferenceKind::Handle
        ? QStringLiteral("https://www.youtube.com/%1/videos").arg(reference.value)
        : QStringLiteral("https://www.youtube.com/channel/%1/videos").arg(reference.value);
}

int keylessPageStart(const QString &pageToken)
{
    static const QRegularExpression expression(QStringLiteral("^ytdlp:(\\d+)$"));
    const QRegularExpressionMatch match = expression.match(pageToken);
    if (!match.hasMatch())
        return 1;
    bool valid = false;
    const int start = match.captured(1).toInt(&valid);
    return valid ? qMax(1, start) : 1;
}
}

YouTubeClient::YouTubeClient(QObject *parent)
    : QObject(parent)
    , m_ytDlpExecutable(QStandardPaths::findExecutable(QStringLiteral("yt-dlp")))
{
    qCDebug(youTubeLog) << "yt-dlp executable" << m_ytDlpExecutable;
}

void YouTubeClient::setApiKey(QString apiKey)
{
    m_apiKey = apiKey.trimmed();
}

bool YouTubeClient::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

bool YouTubeClient::canFetchHistory() const
{
    return hasApiKey() || !m_ytDlpExecutable.isEmpty();
}

void YouTubeClient::resolveChannel(const QString &input, ResolveCallback callback)
{
    QString parseError;
    const std::optional<ChannelReference> reference = parseChannelReference(input, &parseError);
    if (!reference) {
        callback(std::nullopt, parseError);
        return;
    }

    if (!hasApiKey()) {
        resolveChannelKeyless(input, *reference, std::move(callback));
        return;
    }

    const QString filter = reference->kind == ChannelReferenceKind::Handle
        ? QStringLiteral("forHandle")
        : QStringLiteral("id");
    QUrl url = apiUrl(
        QStringLiteral("channels"),
        {
            {QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails")},
            {filter, reference->value},
            {QStringLiteral("fields"),
             QStringLiteral("items(id,snippet(title,customUrl,thumbnails),contentDetails/relatedPlaylists/uploads)")},
        });
    getJson(std::move(url), [input, callback = std::move(callback)](QByteArray json, QString error) {
        if (!error.isEmpty()) {
            callback(std::nullopt, std::move(error));
            return;
        }
        QString parseError;
        std::optional<Channel> channel = parseChannelResponse(json, input, &parseError);
        callback(std::move(channel), std::move(parseError));
    });
}

void YouTubeClient::fetchUploadPage(
    const Channel &channel,
    const QString &pageToken,
    UploadPageCallback callback)
{
    if (!hasApiKey()) {
        if (pageToken.isEmpty())
            fetchFeed(channel, std::move(callback));
        else
            fetchYtDlpUploadPage(
                channel,
                keylessPageStart(pageToken),
                false,
                std::move(callback));
        return;
    }

    QList<QPair<QString, QString>> items = {
        {QStringLiteral("part"), QStringLiteral("contentDetails")},
        {QStringLiteral("playlistId"), channel.uploadsPlaylistId},
        {QStringLiteral("maxResults"), QStringLiteral("50")},
        {QStringLiteral("fields"), QStringLiteral("nextPageToken,items/contentDetails/videoId")},
    };
    if (!pageToken.isEmpty() && !pageToken.startsWith(QStringLiteral("ytdlp:")))
        items.append({QStringLiteral("pageToken"), pageToken});
    QUrl url = apiUrl(QStringLiteral("playlistItems"), items);
    getJson(std::move(url), [callback = std::move(callback)](QByteArray json, QString error) {
        if (!error.isEmpty()) {
            callback({}, std::move(error));
            return;
        }
        QString parseError;
        const UploadPage page = parseUploadPage(json, &parseError);
        callback(page, std::move(parseError));
    });
}

void YouTubeClient::fetchVideos(const QStringList &videoIds, VideosCallback callback)
{
    if (videoIds.isEmpty()) {
        callback({}, {});
        return;
    }
    if (!hasApiKey()) {
        callback({}, QStringLiteral("Video details require yt-dlp in keyless mode."));
        return;
    }

    QUrl url = apiUrl(
        QStringLiteral("videos"),
        {
            {QStringLiteral("part"), QStringLiteral("snippet,contentDetails,liveStreamingDetails")},
            {QStringLiteral("id"), videoIds.join(QLatin1Char(','))},
            {QStringLiteral("fields"),
             QStringLiteral("items(id,snippet(channelId,channelTitle,title,publishedAt,liveBroadcastContent),contentDetails/duration,liveStreamingDetails)")},
        });
    getJson(std::move(url), [callback = std::move(callback)](QByteArray json, QString error) {
        if (!error.isEmpty()) {
            callback({}, std::move(error));
            return;
        }
        QString parseError;
        QList<Video> videos = parseVideosResponse(json, &parseError);
        callback(std::move(videos), std::move(parseError));
    });
}

void YouTubeClient::fetchLiveChannel(const Channel &channel, LiveCallback callback)
{
    if (!hasApiKey()) {
        if (m_ytDlpExecutable.isEmpty()) {
            callback(std::nullopt, QStringLiteral("yt-dlp is not installed."));
            return;
        }
        runYtDlp(
            {
                QStringLiteral("--flat-playlist"),
                QStringLiteral("--playlist-items"),
                QStringLiteral("1:10"),
                QStringLiteral("--dump-json"),
                channelTabUrl(channel, QStringLiteral("streams")).toString(),
            },
            30000,
            true,
            [channel, callback = std::move(callback)](QByteArray json, QString error) {
                if (!error.isEmpty()) {
                    callback(std::nullopt, std::move(error));
                    return;
                }
                QString parseError;
                std::optional<LiveChannel> live = parseYtDlpLive(json, channel, &parseError);
                callback(std::move(live), std::move(parseError));
            });
        return;
    }

    QUrl url = apiUrl(
        QStringLiteral("search"),
        {
            {QStringLiteral("part"), QStringLiteral("snippet")},
            {QStringLiteral("channelId"), channel.id},
            {QStringLiteral("type"), QStringLiteral("video")},
            {QStringLiteral("eventType"), QStringLiteral("live")},
            {QStringLiteral("maxResults"), QStringLiteral("1")},
            {QStringLiteral("fields"), QStringLiteral("items(id/videoId,snippet/title)")},
        });
    getJson(std::move(url), [channel, callback = std::move(callback)](QByteArray json, QString error) {
        if (!error.isEmpty()) {
            callback(std::nullopt, std::move(error));
            return;
        }
        QString parseError;
        std::optional<LiveChannel> live = parseLiveResponse(json, channel, &parseError);
        callback(std::move(live), std::move(parseError));
    });
}

void YouTubeClient::enrichVideos(
    const Channel &channel,
    const QList<Video> &videos,
    VideosCallback callback)
{
    if (hasApiKey() || m_ytDlpExecutable.isEmpty() || videos.isEmpty()) {
        callback({}, {});
        return;
    }

    runYtDlp(
        {
            QStringLiteral("--flat-playlist"),
            QStringLiteral("--playlist-items"),
            QStringLiteral("1:%1").arg(videos.size()),
            QStringLiteral("--dump-json"),
            channelTabUrl(channel, QStringLiteral("videos")).toString(),
        },
        30000,
        false,
        [channel, videos, callback = std::move(callback)](QByteArray json, QString error) {
            if (!error.isEmpty()) {
                callback({}, std::move(error));
                return;
            }
            QString parseError;
            const QList<QJsonObject> objects = parseJsonLines(json, &parseError);
            if (!parseError.isEmpty()) {
                callback({}, std::move(parseError));
                return;
            }
            QHash<QString, QJsonObject> metadata;
            for (const QJsonObject &object : objects)
                metadata.insert(object.value(QStringLiteral("id")).toString(), object);

            QList<Video> enriched;
            for (Video video : videos) {
                const auto item = metadata.constFind(video.id);
                if (item == metadata.cend())
                    continue;
                const QJsonValue duration = item->value(QStringLiteral("duration"));
                if (duration.isDouble() && duration.toDouble() >= 0
                    && duration.toDouble() <= std::numeric_limits<int>::max()) {
                    video.durationSeconds = static_cast<int>(duration.toDouble());
                    video.fetchedAt = QDateTime::currentDateTimeUtc();
                    enriched.append(std::move(video));
                }
            }
            qCDebug(youTubeLog) << "enriched" << enriched.size() << "videos for" << channel.id;
            callback(std::move(enriched), {});
        });
}

void YouTubeClient::resolveChannelKeyless(
    const QString &input,
    const ChannelReference &reference,
    ResolveCallback callback)
{
    if (reference.kind == ChannelReferenceKind::Handle) {
        resolveChannelWithYtDlp(input, std::move(callback));
        return;
    }

    const QUrl url = longFormYouTubeFeedUrl(reference.value);
    getData(url, [this, input, reference, callback = std::move(callback)](
                     QByteArray xml,
                     QString error) mutable {
        if (!error.isEmpty()) {
            if (!m_ytDlpExecutable.isEmpty()) {
                resolveChannelWithYtDlp(input, std::move(callback));
                return;
            }
            callback(std::nullopt, std::move(error));
            return;
        }
        QString parseError;
        const std::optional<YouTubeFeed> feed = parseYouTubeFeed(xml, &parseError);
        if (!feed || feed->channelId != reference.value) {
            callback(
                std::nullopt,
                parseError.isEmpty()
                    ? QStringLiteral("YouTube feed returned a different channel.")
                    : std::move(parseError));
            return;
        }
        callback(
            Channel{
                feed->channelId,
                input.trimmed(),
                {},
                feed->channelTitle,
                {},
                uploadsPlaylistId(feed->channelId),
                QDateTime::currentDateTimeUtc(),
            },
            {});
    });
}

void YouTubeClient::resolveChannelWithYtDlp(const QString &input, ResolveCallback callback)
{
    if (m_ytDlpExecutable.isEmpty()) {
        callback(
            std::nullopt,
            QStringLiteral("yt-dlp is required to resolve YouTube channel handles."));
        return;
    }
    QString parseError;
    const std::optional<ChannelReference> reference = parseChannelReference(input, &parseError);
    if (!reference) {
        callback(std::nullopt, std::move(parseError));
        return;
    }
    runYtDlp(
        {
            QStringLiteral("--flat-playlist"),
            QStringLiteral("--playlist-items"),
            QStringLiteral("1"),
            QStringLiteral("--dump-single-json"),
            channelUrlForReference(*reference),
        },
        30000,
        true,
        [input, callback = std::move(callback)](QByteArray json, QString error) {
            if (!error.isEmpty()) {
                callback(std::nullopt, std::move(error));
                return;
            }
            QString parseError;
            std::optional<Channel> channel = parseYtDlpChannel(json, input, &parseError);
            callback(std::move(channel), std::move(parseError));
        });
}

void YouTubeClient::fetchFeed(const Channel &channel, UploadPageCallback callback)
{
    const QUrl url = longFormYouTubeFeedUrl(channel.id);
    if (url.isEmpty()) {
        callback({}, QStringLiteral("Stored YouTube channel ID is invalid."));
        return;
    }
    qCDebug(youTubeLog) << "fetching Atom feed for" << channel.id;
    getData(url, [this, channel, callback = std::move(callback)](
                     QByteArray xml,
                     QString error) mutable {
        if (!error.isEmpty()) {
            if (!m_ytDlpExecutable.isEmpty()) {
                qCDebug(youTubeLog) << "Atom feed failed for" << channel.id
                                    << "falling back to yt-dlp:" << error;
                fetchYtDlpUploadPage(channel, 1, false, std::move(callback));
                return;
            }
            callback({}, std::move(error));
            return;
        }
        QString parseError;
        std::optional<YouTubeFeed> feed = parseYouTubeFeed(xml, &parseError);
        if (!feed || feed->channelId != channel.id) {
            callback(
                {},
                parseError.isEmpty()
                    ? QStringLiteral("YouTube feed returned a different channel.")
                    : std::move(parseError));
            return;
        }
        UploadPage page;
        page.videos = std::move(feed->videos);
        if (!m_ytDlpExecutable.isEmpty()) {
            page.nextPageToken = QStringLiteral("ytdlp:%1").arg(page.videos.size() + 1);
        }
        qCDebug(youTubeLog) << "Atom feed returned" << page.videos.size() << "videos for"
                            << channel.id;
        callback(std::move(page), {});
    });
}

void YouTubeClient::fetchYtDlpUploadPage(
    const Channel &channel,
    int startIndex,
    bool flat,
    UploadPageCallback callback)
{
    if (m_ytDlpExecutable.isEmpty()) {
        callback({}, QStringLiteral("yt-dlp is not installed."));
        return;
    }
    const int lastIndex = startIndex + keylessHistoryPageSize - 1;
    QStringList arguments;
    if (flat) {
        arguments.append(QStringLiteral("--flat-playlist"));
        arguments.append(QStringLiteral("--dump-json"));
    } else {
        arguments.append({
            QStringLiteral("--print"),
            QStringLiteral(
                "{\"id\":%(id|null)j,\"title\":%(title|null)j,"
                "\"channel_id\":%(channel_id|null)j,\"channel\":%(channel|null)j,"
                "\"release_timestamp\":%(release_timestamp|null)j,"
                "\"timestamp\":%(timestamp|null)j,\"upload_date\":%(upload_date|null)j,"
                "\"duration\":%(duration|null)j,\"live_status\":%(live_status|null)j,"
                "\"is_live\":%(is_live|null)j}"),
        });
    }
    arguments.append({
        QStringLiteral("--playlist-items"),
        QStringLiteral("%1:%2").arg(startIndex).arg(lastIndex),
        channelTabUrl(channel, QStringLiteral("videos")).toString(),
    });
    qCDebug(youTubeLog) << "fetching yt-dlp uploads" << channel.id << startIndex << lastIndex;
    runYtDlp(
        std::move(arguments),
        flat ? 30000 : 120000,
        true,
        [channel, startIndex, callback = std::move(callback)](
            QByteArray json,
            QString error) {
            if (!error.isEmpty()) {
                callback({}, std::move(error));
                return;
            }
            QString parseError;
            const QList<QJsonObject> objects = parseJsonLines(json, &parseError);
            if (!parseError.isEmpty()) {
                callback({}, std::move(parseError));
                return;
            }
            UploadPage page;
            page.videos = parseYtDlpVideos(json, channel, &parseError);
            if (!parseError.isEmpty()) {
                callback({}, std::move(parseError));
                return;
            }
            if (!objects.isEmpty() && page.videos.isEmpty()) {
                callback({}, QStringLiteral("yt-dlp returned videos without publication dates."));
                return;
            }
            if (objects.size() == keylessHistoryPageSize) {
                page.nextPageToken =
                    QStringLiteral("ytdlp:%1").arg(startIndex + objects.size());
            }
            callback(std::move(page), {});
        });
}

std::optional<ChannelReference> YouTubeClient::parseChannelReference(
    const QString &input,
    QString *error)
{
    QString candidate = input.trimmed();
    if (candidate.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || candidate.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        const QUrl url(candidate);
        QString host = url.host().toLower();
        if (host.startsWith(QStringLiteral("www.")))
            host.remove(0, 4);
        if (host != QStringLiteral("youtube.com") && host != QStringLiteral("m.youtube.com")) {
            setError(error, QStringLiteral("Enter a YouTube channel URL, handle, or channel ID."));
            return std::nullopt;
        }

        const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() == 1 && parts.first().startsWith(QLatin1Char('@')))
            candidate = parts.first();
        else if (parts.size() == 2 && parts.first() == QStringLiteral("channel"))
            candidate = parts.at(1);
        else {
            setError(error, QStringLiteral("Enter a channel page, not a video or playlist URL."));
            return std::nullopt;
        }
    }

    static const QRegularExpression channelIdExpression(
        QStringLiteral("^UC[A-Za-z0-9_-]{22}$"));
    static const QRegularExpression handleExpression(
        QStringLiteral("^@?([^\\s/@?#]{3,30})$"));
    if (channelIdExpression.match(candidate).hasMatch())
        return ChannelReference{ChannelReferenceKind::Id, candidate};
    const QRegularExpressionMatch handleMatch = handleExpression.match(candidate);
    if (handleMatch.hasMatch())
        return ChannelReference{ChannelReferenceKind::Handle,
                                QStringLiteral("@").append(handleMatch.captured(1))};

    setError(error, QStringLiteral("Enter a valid handle or UC channel ID."));
    return std::nullopt;
}

std::optional<Channel> YouTubeClient::parseChannelResponse(
    const QByteArray &json,
    const QString &originalInput,
    QString *error)
{
    const std::optional<QJsonObject> root = parseObject(json, error);
    if (!root)
        return std::nullopt;
    const QJsonArray items = root->value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) {
        setError(error, QStringLiteral("YouTube channel was not found."));
        return std::nullopt;
    }

    const QJsonObject item = items.first().toObject();
    const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
    const QString uploadsPlaylistId = item.value(QStringLiteral("contentDetails"))
                                          .toObject()
                                          .value(QStringLiteral("relatedPlaylists"))
                                          .toObject()
                                          .value(QStringLiteral("uploads"))
                                          .toString();
    Channel channel{
        item.value(QStringLiteral("id")).toString(),
        originalInput.trimmed(),
        snippet.value(QStringLiteral("customUrl")).toString(),
        snippet.value(QStringLiteral("title")).toString(),
        thumbnailUrl(snippet),
        uploadsPlaylistId,
        QDateTime::currentDateTimeUtc(),
    };
    if (channel.id.isEmpty() || channel.title.isEmpty() || channel.uploadsPlaylistId.isEmpty()) {
        setError(error, QStringLiteral("YouTube returned incomplete channel metadata."));
        return std::nullopt;
    }
    return channel;
}

UploadPage YouTubeClient::parseUploadPage(const QByteArray &json, QString *error)
{
    UploadPage page;
    const std::optional<QJsonObject> root = parseObject(json, error);
    if (!root)
        return page;
    page.nextPageToken = root->value(QStringLiteral("nextPageToken")).toString();
    const QJsonArray items = root->value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QString id = value.toObject()
                               .value(QStringLiteral("contentDetails"))
                               .toObject()
                               .value(QStringLiteral("videoId"))
                               .toString();
        if (!id.isEmpty())
            page.videoIds.append(id);
    }
    return page;
}

QStringList YouTubeClient::parseUploadVideoIds(const QByteArray &json, QString *error)
{
    return parseUploadPage(json, error).videoIds;
}

QList<Video> YouTubeClient::parseVideosResponse(const QByteArray &json, QString *error)
{
    QList<Video> result;
    const std::optional<QJsonObject> root = parseObject(json, error);
    if (!root)
        return result;
    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    const QJsonArray items = root->value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QJsonObject snippet = item.value(QStringLiteral("snippet")).toObject();
        const Video video{
            item.value(QStringLiteral("id")).toString(),
            snippet.value(QStringLiteral("channelId")).toString(),
            snippet.value(QStringLiteral("channelTitle")).toString(),
            snippet.value(QStringLiteral("title")).toString(),
            QDateTime::fromString(
                snippet.value(QStringLiteral("publishedAt")).toString(),
                Qt::ISODate)
                .toUTC(),
            item.contains(QStringLiteral("liveStreamingDetails")),
            snippet.value(QStringLiteral("liveBroadcastContent")).toString(),
            fetchedAt,
            durationSeconds(item.value(QStringLiteral("contentDetails"))
                                .toObject()
                                .value(QStringLiteral("duration"))
                                .toString()),
        };
        if (!video.id.isEmpty() && !video.channelId.isEmpty() && !video.title.isEmpty()
            && video.publishedAt.isValid()) {
            result.append(video);
        }
    }
    return result;
}

std::optional<LiveChannel> YouTubeClient::parseLiveResponse(
    const QByteArray &json,
    const Channel &channel,
    QString *error)
{
    const std::optional<QJsonObject> root = parseObject(json, error);
    if (!root)
        return std::nullopt;
    const QJsonArray items = root->value(QStringLiteral("items")).toArray();
    if (items.isEmpty())
        return std::nullopt;

    const QJsonObject item = items.first().toObject();
    const QString videoId = item.value(QStringLiteral("id"))
                                .toObject()
                                .value(QStringLiteral("videoId"))
                                .toString();
    const QString title = item.value(QStringLiteral("snippet"))
                              .toObject()
                              .value(QStringLiteral("title"))
                              .toString();
    if (videoId.isEmpty() || title.isEmpty()) {
        setError(error, QStringLiteral("YouTube returned incomplete live-stream metadata."));
        return std::nullopt;
    }
    return LiveChannel{channel.id, channel.title, channel.avatarUrl, videoId, title};
}

std::optional<Channel> YouTubeClient::parseYtDlpChannel(
    const QByteArray &json,
    const QString &originalInput,
    QString *error)
{
    if (error)
        error->clear();
    const std::optional<QJsonObject> root = parseObject(json, error);
    if (!root)
        return std::nullopt;
    const QJsonArray entries = root->value(QStringLiteral("entries")).toArray();
    const QJsonObject firstEntry = entries.isEmpty() ? QJsonObject{} : entries.first().toObject();
    const auto firstString = [&root, &firstEntry](const QStringList &names) {
        for (const QString &name : names) {
            const QString rootValue = root->value(name).toString().trimmed();
            if (!rootValue.isEmpty())
                return rootValue;
            const QString entryValue = firstEntry.value(name).toString().trimmed();
            if (!entryValue.isEmpty())
                return entryValue;
        }
        return QString{};
    };

    QString channelId = firstString({
        QStringLiteral("channel_id"),
        QStringLiteral("playlist_channel_id"),
    });
    if (channelId.isEmpty() && root->value(QStringLiteral("id")).toString().startsWith(QStringLiteral("UC")))
        channelId = root->value(QStringLiteral("id")).toString();
    const QString channelTitle = firstString({
        QStringLiteral("channel"),
        QStringLiteral("playlist_channel"),
        QStringLiteral("uploader"),
        QStringLiteral("playlist_uploader"),
    });
    const QString playlistId = uploadsPlaylistId(channelId);
    if (longFormYouTubeFeedUrl(channelId).isEmpty() || channelTitle.isEmpty()
        || playlistId.isEmpty()) {
        setError(error, QStringLiteral("yt-dlp returned incomplete channel metadata."));
        return std::nullopt;
    }

    QString handle = firstString({
        QStringLiteral("uploader_id"),
        QStringLiteral("playlist_uploader_id"),
    });
    if (!handle.startsWith(QLatin1Char('@')))
        handle.clear();
    QString avatarUrl = ytDlpThumbnail(*root);
    if (avatarUrl.isEmpty())
        avatarUrl = ytDlpThumbnail(firstEntry);
    return Channel{
        channelId,
        originalInput.trimmed(),
        handle,
        channelTitle,
        avatarUrl,
        playlistId,
        QDateTime::currentDateTimeUtc(),
    };
}

QList<Video> YouTubeClient::parseYtDlpVideos(
    const QByteArray &json,
    const Channel &channel,
    QString *error)
{
    const QList<QJsonObject> objects = parseJsonLines(json, error);
    if (error && !error->isEmpty())
        return {};
    QList<Video> videos;
    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    for (const QJsonObject &object : objects) {
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString title = object.value(QStringLiteral("title")).toString();
        const QDateTime publishedAt = ytDlpPublishedAt(object);
        if (id.isEmpty() || title.isEmpty() || !publishedAt.isValid())
            continue;
        const QString liveStatus = object.value(QStringLiteral("live_status")).toString();
        const bool isBroadcast = !liveStatus.isEmpty() && liveStatus != QStringLiteral("not_live");
        QString broadcastState = QStringLiteral("none");
        if (liveStatus == QStringLiteral("is_live"))
            broadcastState = QStringLiteral("live");
        else if (liveStatus == QStringLiteral("is_upcoming"))
            broadcastState = QStringLiteral("upcoming");
        int seconds = -1;
        const QJsonValue duration = object.value(QStringLiteral("duration"));
        if (duration.isDouble() && duration.toDouble() >= 0
            && duration.toDouble() <= std::numeric_limits<int>::max()) {
            seconds = static_cast<int>(duration.toDouble());
        }
        const QString resultChannelId =
            object.value(QStringLiteral("channel_id")).toString(channel.id);
        const QString resultChannelTitle =
            object.value(QStringLiteral("channel")).toString(channel.title);
        videos.append(Video{
            id,
            resultChannelId,
            resultChannelTitle,
            title,
            publishedAt,
            isBroadcast,
            broadcastState,
            fetchedAt,
            seconds,
        });
    }
    return videos;
}

std::optional<LiveChannel> YouTubeClient::parseYtDlpLive(
    const QByteArray &json,
    const Channel &channel,
    QString *error)
{
    const QList<QJsonObject> objects = parseJsonLines(json, error);
    if (error && !error->isEmpty())
        return std::nullopt;
    for (const QJsonObject &object : objects) {
        if (object.value(QStringLiteral("live_status")).toString() != QStringLiteral("is_live")
            && !object.value(QStringLiteral("is_live")).toBool()) {
            continue;
        }
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString title = object.value(QStringLiteral("title")).toString();
        if (!id.isEmpty() && !title.isEmpty())
            return LiveChannel{channel.id, channel.title, channel.avatarUrl, id, title};
    }
    return std::nullopt;
}

void YouTubeClient::getData(QUrl url, DataCallback callback)
{
    if (!url.isValid() || url.isEmpty()) {
        callback({}, QStringLiteral("YouTube request URL is invalid."));
        return;
    }

    QNetworkRequest request(std::move(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmaTube/0.1"));
    request.setTransferTimeout(15000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    auto body = std::make_shared<QByteArray>();
    auto exceededSizeLimit = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, body, exceededSizeLimit] {
        const QByteArray chunk = reply->readAll();
        if (body->size() + chunk.size() > maximumResponseSize) {
            *exceededSizeLimit = true;
            reply->abort();
            return;
        }
        body->append(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [reply, body, exceededSizeLimit, callback = std::move(callback)] {
        if (!*exceededSizeLimit) {
            const QByteArray finalChunk = reply->readAll();
            if (body->size() + finalChunk.size() > maximumResponseSize)
                *exceededSizeLimit = true;
            else
                body->append(finalChunk);
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (*exceededSizeLimit) {
            error = QStringLiteral("YouTube response exceeded the size limit.");
        } else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            error = status > 0
                ? QStringLiteral("YouTube request failed with HTTP %1.").arg(status)
                : reply->errorString();
        }
        reply->deleteLater();
        callback(*body, error);
    });
}

void YouTubeClient::getJson(QUrl url, DataCallback callback)
{
    if (!hasApiKey()) {
        callback({}, QStringLiteral("YouTube API key is not configured."));
        return;
    }
    getData(
        std::move(url),
        [callback = std::move(callback)](QByteArray body, QString error) {
            if (!error.isEmpty()) {
                const QString apiError = apiErrorMessage(body);
                if (!apiError.isEmpty())
                    error = apiError;
            }
            callback(std::move(body), std::move(error));
        });
}

void YouTubeClient::runYtDlp(
    QStringList arguments,
    int timeoutMs,
    bool highPriority,
    DataCallback callback)
{
    if (m_ytDlpExecutable.isEmpty()) {
        callback({}, QStringLiteral("yt-dlp is not installed."));
        return;
    }
    arguments.prepend(QStringLiteral("--no-warnings"));
    arguments.prepend(QStringLiteral("--ignore-config"));
    YtDlpJob job{std::move(arguments), timeoutMs, std::move(callback)};
    if (highPriority)
        m_ytDlpQueue.prepend(std::move(job));
    else
        m_ytDlpQueue.enqueue(std::move(job));
    dispatchYtDlp();
}

void YouTubeClient::dispatchYtDlp()
{
    while (m_ytDlpInFlight < maximumYtDlpProcesses && !m_ytDlpQueue.isEmpty()) {
        YtDlpJob job = m_ytDlpQueue.dequeue();
        ++m_ytDlpInFlight;
        QProcess *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QTimer *timer = new QTimer(process);
        timer->setSingleShot(true);
        const auto completed = std::make_shared<bool>(false);
        auto complete = [
                                  this,
                                  process,
                                  timer,
                                  completed,
                                  callback = std::move(job.callback)](QString forcedError) mutable {
            if (std::exchange(*completed, true))
                return;
            timer->stop();
            QByteArray output = process->readAllStandardOutput();
            QString error = std::move(forcedError);
            if (output.size() > maximumYtDlpOutputSize) {
                output.clear();
                error = QStringLiteral("yt-dlp output exceeded the size limit.");
            }
            if (error.isEmpty() && process->exitStatus() != QProcess::NormalExit)
                error = QStringLiteral("yt-dlp crashed.");
            if (error.isEmpty() && process->exitCode() != 0) {
                error = QString::fromUtf8(process->readAllStandardError()).trimmed();
                if (error.isEmpty())
                    error = QStringLiteral("yt-dlp exited with code %1.").arg(process->exitCode());
            }
            qCDebug(youTubeLog) << "yt-dlp completed with" << output.size() << "bytes"
                                << (error.isEmpty() ? QStringLiteral("success") : error);
            --m_ytDlpInFlight;
            process->deleteLater();
            callback(std::move(output), std::move(error));
            dispatchYtDlp();
        };
        connect(
            process,
            &QProcess::errorOccurred,
            this,
            [complete](QProcess::ProcessError processError) mutable {
                if (processError == QProcess::FailedToStart)
                    complete(QStringLiteral("Could not start yt-dlp."));
            });
        connect(
            process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [complete](int, QProcess::ExitStatus) mutable { complete({}); });
        connect(timer, &QTimer::timeout, this, [process, complete]() mutable {
            process->kill();
            complete(QStringLiteral("yt-dlp timed out."));
        });
        qCDebug(youTubeLog) << "starting yt-dlp" << job.arguments;
        process->start(m_ytDlpExecutable, job.arguments, QIODevice::ReadOnly);
        timer->start(job.timeoutMs);
    }
}

QUrl YouTubeClient::channelTabUrl(const Channel &channel, const QString &tab) const
{
    if (longFormYouTubeFeedUrl(channel.id).isEmpty())
        return {};
    QUrl url(QStringLiteral("https://www.youtube.com"));
    url.setPath(QStringLiteral("/channel/%1/%2").arg(channel.id, tab));
    return url;
}

QUrl YouTubeClient::apiUrl(
    const QString &path,
    const QList<QPair<QString, QString>> &items) const
{
    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/%1").arg(path));
    QUrlQuery query;
    for (const auto &[name, value] : items)
        query.addQueryItem(name, value);
    query.addQueryItem(QStringLiteral("key"), m_apiKey);
    url.setQuery(query);
    return url;
}

QString YouTubeClient::apiErrorMessage(const QByteArray &json)
{
    const std::optional<QJsonObject> root = parseObject(json, nullptr);
    if (!root)
        return {};
    return root->value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

void YouTubeClient::setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}
