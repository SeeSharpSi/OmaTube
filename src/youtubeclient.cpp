#include "youtubeclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <limits>
#include <memory>

namespace {
constexpr qsizetype maximumResponseSize = 2 * 1024 * 1024;

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
}

YouTubeClient::YouTubeClient(QObject *parent)
    : QObject(parent)
{
}

void YouTubeClient::setApiKey(QString apiKey)
{
    m_apiKey = apiKey.trimmed();
}

bool YouTubeClient::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

void YouTubeClient::resolveChannel(const QString &input, ResolveCallback callback)
{
    QString parseError;
    const std::optional<ChannelReference> reference = parseChannelReference(input, &parseError);
    if (!reference) {
        callback(std::nullopt, parseError);
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
    QList<QPair<QString, QString>> items = {
        {QStringLiteral("part"), QStringLiteral("contentDetails")},
        {QStringLiteral("playlistId"), channel.uploadsPlaylistId},
        {QStringLiteral("maxResults"), QStringLiteral("50")},
        {QStringLiteral("fields"), QStringLiteral("nextPageToken,items/contentDetails/videoId")},
    };
    if (!pageToken.isEmpty())
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
            setError(error, QStringLiteral("Enter a YouTube channel URL, @handle, or channel ID."));
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
        QStringLiteral("^@[^\\s/@?#]{3,30}$"));
    if (channelIdExpression.match(candidate).hasMatch())
        return ChannelReference{ChannelReferenceKind::Id, candidate};
    if (handleExpression.match(candidate).hasMatch())
        return ChannelReference{ChannelReferenceKind::Handle, candidate};

    setError(error, QStringLiteral("Enter a valid @handle or UC channel ID."));
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

void YouTubeClient::getJson(QUrl url, JsonCallback callback)
{
    if (!hasApiKey()) {
        callback({}, QStringLiteral("YouTube API key is not configured."));
        return;
    }

    QNetworkRequest request(std::move(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YT Client/0.1"));
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
            error = apiErrorMessage(*body);
            if (error.isEmpty())
                error = reply->errorString();
        }
        reply->deleteLater();
        callback(*body, error);
    });
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
