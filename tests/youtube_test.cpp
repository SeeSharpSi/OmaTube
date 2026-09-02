#include "refreshservice.h"
#include "repository.h"
#include "youtubefeed.h"
#include "youtubeclient.h"

#include <QHash>
#include <QSignalSpy>
#include <QTest>

struct FakeUploadPage
{
    QString requestToken;
    QStringList videoIds;
    QString nextPageToken;
    QString error;
};

class FakeYouTubeClient final : public YouTubeClient
{
public:
    bool hasApiKey() const override { return true; }

    void resolveChannel(const QString &input, ResolveCallback callback) override
    {
        ++resolveCount;
        callback(
            Channel{
                input,
                input,
                QStringLiteral("@refreshed"),
                QStringLiteral("Refreshed channel"),
                {},
                QStringLiteral("UU-refreshed"),
                QDateTime::currentDateTimeUtc(),
            },
            {});
    }

    void fetchUploadPage(
        const Channel &channel,
        const QString &pageToken,
        UploadPageCallback callback) override
    {
        ++uploadPageRequestCount;
        lastRequestedPageToken = pageToken;
        const auto pages = uploadPagesByChannel.constFind(channel.id);
        const auto sourceVideos = sourceVideosByChannel.constFind(channel.id);
        if (pageToken.isEmpty() && sourceVideos != sourceVideosByChannel.constEnd()) {
            callback({{}, sourceNextPageToken, *sourceVideos}, {});
            return;
        }
        if (pages == uploadPagesByChannel.constEnd()) {
            callback({{QStringLiteral("video-%1").arg(channel.id)}, {}, {}}, {});
            return;
        }
        for (const FakeUploadPage &page : *pages) {
            if (page.requestToken == pageToken) {
                callback({page.videoIds, page.nextPageToken, {}}, page.error);
                return;
            }
        }
        callback({}, QStringLiteral("no fake page for token %1").arg(pageToken));
    }

    void fetchVideos(const QStringList &videoIds, VideosCallback callback) override
    {
        ++videoDetailRequestCount;
        QList<Video> videos;
        for (const QString &id : videoIds) {
            const QString channelId = id.mid(QStringLiteral("video-").size());
            videos.append({
                id,
                channelId,
                {},
                QStringLiteral("Video from %1").arg(channelId),
                QDateTime::currentDateTimeUtc().addSecs(-m_publishedAtOffset++),
                false,
                QStringLiteral("none"),
                QDateTime::currentDateTimeUtc(),
                600,
            });
        }
        callback(videos, {});
    }

    void fetchLiveChannel(const Channel &channel, LiveCallback callback) override
    {
        if (failLive) {
            callback(std::nullopt, QStringLiteral("quota exceeded"));
            return;
        }
        if (channel.id == QStringLiteral("UCAlpha")) {
            callback(
                LiveChannel{
                    channel.id,
                    channel.title,
                    channel.avatarUrl,
                    QStringLiteral("live-id"),
                    QStringLiteral("Live now"),
                },
                {});
            return;
        }
        callback(std::nullopt, {});
    }

    bool failLive = false;
    int resolveCount = 0;
    int uploadPageRequestCount = 0;
    int videoDetailRequestCount = 0;
    QString lastRequestedPageToken;
    QHash<QString, QList<FakeUploadPage>> uploadPagesByChannel;
    QHash<QString, QList<Video>> sourceVideosByChannel;
    QString sourceNextPageToken;

private:
    int m_publishedAtOffset = 0;
};

class YouTubeTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesChannelReferences_data();
    void parsesChannelReferences();
    void rejectsInvalidChannelReference();
    void parsesApiResponses();
    void refreshServiceStoresFeedAndLiveSnapshot();
    void refreshServiceReportsIncompleteLiveStatus();
    void refreshServiceStoresSourceVideosImmediately();
    void refreshServiceKeepsLiveStateOnFailure();
    void refreshServiceRefreshesStaleChannelMetadata();
    void refreshServiceInitializesChannelHistoryState();
    void loadOlderFetchesNextUploadPages();
    void loadOlderScopesToCategoryChannels();
    void loadOlderKeepsTokenOnFailure();
    void loadOlderSkipsCompleteChannels();
    void generatesLongFormFeedUrls();
    void parsesAtomFeed();
    void ignoresMalformedFeedEntries();
    void rejectsInvalidFeedXml();
    void parsesYtDlpResponses();
};

namespace {
Channel makeChannel(const QString &id, const QString &title)
{
    return {
        id,
        QStringLiteral("@input"),
        QStringLiteral("@handle"),
        title,
        QStringLiteral("https://example.com/avatar.jpg"),
        QStringLiteral("UU%1").arg(id),
        QDateTime::currentDateTimeUtc(),
    };
}
}

void YouTubeTest::parsesChannelReferences_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("value");

    QTest::newRow("handle") << QStringLiteral("@QtGroup")
                             << static_cast<int>(ChannelReferenceKind::Handle)
                             << QStringLiteral("@QtGroup");
    QTest::newRow("bare-handle") << QStringLiteral("QtGroup")
                                  << static_cast<int>(ChannelReferenceKind::Handle)
                                  << QStringLiteral("@QtGroup");
    QTest::newRow("handle-url") << QStringLiteral("https://www.youtube.com/@QtGroup")
                                 << static_cast<int>(ChannelReferenceKind::Handle)
                                 << QStringLiteral("@QtGroup");
    QTest::newRow("unicode-handle") << QStringLiteral("@日本語")
                                     << static_cast<int>(ChannelReferenceKind::Handle)
                                     << QStringLiteral("@日本語");
    QTest::newRow("channel-url")
        << QStringLiteral("https://youtube.com/channel/UC1234567890123456789012")
        << static_cast<int>(ChannelReferenceKind::Id)
        << QStringLiteral("UC1234567890123456789012");
}

void YouTubeTest::parsesChannelReferences()
{
    QFETCH(QString, input);
    QFETCH(int, kind);
    QFETCH(QString, value);
    QString error;
    const std::optional<ChannelReference> reference =
        YouTubeClient::parseChannelReference(input, &error);
    QVERIFY2(reference.has_value(), qPrintable(error));
    QCOMPARE(static_cast<int>(reference->kind), kind);
    QCOMPARE(reference->value, value);
}

void YouTubeTest::rejectsInvalidChannelReference()
{
    QString error;
    QVERIFY(!YouTubeClient::parseChannelReference(
        QStringLiteral("https://youtube.com/watch?v=abc"), &error));
    QCOMPARE(error, QStringLiteral("Enter a channel page, not a video or playlist URL."));

    QVERIFY(!YouTubeClient::parseChannelReference(QStringLiteral("Qt"), &error));
    QCOMPARE(error, QStringLiteral("Enter a valid handle or UC channel ID."));
}

void YouTubeTest::parsesApiResponses()
{
    const QByteArray channelJson = R"({"items":[{"id":"UC1234567890123456789012","snippet":{"title":"Qt","customUrl":"@QtGroup","thumbnails":{"default":{"url":"https://example.com/a.jpg"}}},"contentDetails":{"relatedPlaylists":{"uploads":"UU123"}}}]})";
    QString error;
    const std::optional<Channel> channel =
        YouTubeClient::parseChannelResponse(channelJson, QStringLiteral("@QtGroup"), &error);
    QVERIFY2(channel.has_value(), qPrintable(error));
    QCOMPARE(channel->title, QStringLiteral("Qt"));
    QCOMPARE(channel->uploadsPlaylistId, QStringLiteral("UU123"));

    const QByteArray uploadJson = R"({"items":[{"contentDetails":{"videoId":"one"}},{"contentDetails":{"videoId":"two"}}]})";
    QCOMPARE(YouTubeClient::parseUploadVideoIds(uploadJson, &error),
             QStringList({QStringLiteral("one"), QStringLiteral("two")}));

    const QByteArray uploadPageJson = R"({"nextPageToken":"CAUQAA","items":[{"contentDetails":{"videoId":"one"}},{"contentDetails":{"videoId":"two"}}]})";
    const UploadPage uploadPage = YouTubeClient::parseUploadPage(uploadPageJson, &error);
    QCOMPARE(uploadPage.videoIds, QStringList({QStringLiteral("one"), QStringLiteral("two")}));
    QCOMPARE(uploadPage.nextPageToken, QStringLiteral("CAUQAA"));

    const QByteArray lastUploadPageJson = R"({"items":[{"contentDetails":{"videoId":"three"}}]})";
    const UploadPage lastUploadPage = YouTubeClient::parseUploadPage(lastUploadPageJson, &error);
    QCOMPARE(lastUploadPage.videoIds, QStringList({QStringLiteral("three")}));
    QVERIFY(lastUploadPage.nextPageToken.isEmpty());

    const QByteArray videosJson = R"({"items":[{"id":"regular","snippet":{"channelId":"UC1234567890123456789012","channelTitle":"Qt","title":"Regular","publishedAt":"2026-08-24T12:00:00Z","liveBroadcastContent":"none"},"contentDetails":{"duration":"PT3M1S"}},{"id":"stream","snippet":{"channelId":"UC1234567890123456789012","channelTitle":"Qt","title":"Stream","publishedAt":"2026-08-24T13:00:00Z","liveBroadcastContent":"live"},"contentDetails":{"duration":"PT1H2M3S"},"liveStreamingDetails":{"actualStartTime":"2026-08-24T13:00:00Z"}}]})";
    const QList<Video> videos = YouTubeClient::parseVideosResponse(videosJson, &error);
    QCOMPARE(videos.size(), 2);
    QVERIFY(!videos.first().isBroadcast);
    QCOMPARE(videos.first().durationSeconds, 181);
    QVERIFY(videos.last().isBroadcast);
    QCOMPARE(videos.last().durationSeconds, 3723);

    const QByteArray liveJson = R"({"items":[{"id":{"videoId":"live-video"},"snippet":{"title":"Live now"}}]})";
    const std::optional<LiveChannel> live =
        YouTubeClient::parseLiveResponse(liveJson, *channel, &error);
    QVERIFY2(live.has_value(), qPrintable(error));
    QCOMPARE(live->videoId, QStringLiteral("live-video"));
}

void YouTubeTest::refreshServiceStoresFeedAndLiveSnapshot()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta")), &error));
    FakeYouTubeClient client;
    RefreshService service(&repository, &client);

    QList<LiveChannel> liveChannels;
    bool liveComplete = false;
    connect(&service, &RefreshService::refreshFinished, this,
            [&](const QList<LiveChannel> &live, bool complete, const QString &, const QString &) {
                liveChannels = live;
                liveComplete = complete;
            });
    service.refresh();

    QVERIFY(!service.refreshing());
    QCOMPARE(repository.feed().size(), 2);
    QCOMPARE(liveChannels.size(), 1);
    QCOMPARE(liveChannels.first().channelId, QStringLiteral("UCAlpha"));
    QVERIFY(liveComplete);
}

void YouTubeTest::refreshServiceReportsIncompleteLiveStatus()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    FakeYouTubeClient client;
    client.failLive = true;
    RefreshService service(&repository, &client);

    bool liveComplete = true;
    QString liveError;
    connect(&service, &RefreshService::refreshFinished, this,
            [&](const QList<LiveChannel> &, bool complete, const QString &, const QString &errorText) {
                liveComplete = complete;
                liveError = errorText;
            });
    service.refresh();

    QVERIFY(!liveComplete);
    QVERIFY(liveError.contains(QStringLiteral("quota exceeded")));
}

void YouTubeTest::refreshServiceStoresSourceVideosImmediately()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const Channel channel = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha"));
    QVERIFY(repository.upsertChannel(channel, &error));
    FakeYouTubeClient client;
    client.sourceNextPageToken = QStringLiteral("ytdlp:2");
    client.sourceVideosByChannel.insert(
        channel.id,
        {Video{
            QStringLiteral("source-video"),
            channel.id,
            channel.title,
            QStringLiteral("Source video"),
            QDateTime::currentDateTimeUtc(),
            false,
            QStringLiteral("none"),
            QDateTime::currentDateTimeUtc(),
            -1,
        }});
    RefreshService service(&repository, &client);
    QSignalSpy feedSpy(&service, &RefreshService::feedChanged);

    service.refresh();

    QCOMPARE(client.videoDetailRequestCount, 0);
    QCOMPARE(repository.feed(std::nullopt, 180).size(), 1);
    QVERIFY(feedSpy.count() >= 1);
    const QList<ChannelHistoryState> states = repository.channelHistoryStates(&error);
    QCOMPARE(states.first().nextPageToken, QStringLiteral("ytdlp:2"));
}

void YouTubeTest::refreshServiceKeepsLiveStateOnFailure()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(
        makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")),
        &error));
    FakeYouTubeClient client;
    RefreshService service(&repository, &client);
    QList<LiveChannel> live;
    connect(
        &service,
        &RefreshService::refreshFinished,
        this,
        [&live](QList<LiveChannel> channels, bool, const QString &, const QString &) {
            live = std::move(channels);
        });

    service.refresh();
    QCOMPARE(live.size(), 1);
    client.failLive = true;
    service.refresh();

    QCOMPARE(live.size(), 1);
    QCOMPARE(live.first().channelId, QStringLiteral("UCAlpha"));
}

void YouTubeTest::refreshServiceRefreshesStaleChannelMetadata()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    Channel stale = makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Old title"));
    stale.metadataFetchedAt = QDateTime::currentDateTimeUtc().addDays(-30);
    QVERIFY(repository.upsertChannel(stale, &error));
    FakeYouTubeClient client;
    RefreshService service(&repository, &client);

    service.refresh();

    QCOMPARE(client.resolveCount, 1);
    const QList<Channel> channels = repository.channels();
    QCOMPARE(channels.size(), 1);
    QCOMPARE(channels.first().title, QStringLiteral("Refreshed channel"));
    QCOMPARE(channels.first().originalInput, stale.originalInput);
}

void YouTubeTest::refreshServiceInitializesChannelHistoryState()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    FakeYouTubeClient client;
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCAlpha"),
        {FakeUploadPage{
            {},
            {QStringLiteral("video-UCAlpha")},
            QStringLiteral("token-two"),
            {}}});
    RefreshService service(&repository, &client);

    service.refresh();

    QList<ChannelHistoryState> states = repository.channelHistoryStates(&error);
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.first().channelId, QStringLiteral("UCAlpha"));
    QCOMPARE(states.first().nextPageToken, QStringLiteral("token-two"));
    QVERIFY(!states.first().historyComplete);

    // A regular refresh must not rewind a cursor that deeper loads advanced.
    QVERIFY(repository.setChannelHistoryState(
        QStringLiteral("UCAlpha"), QStringLiteral("token-seven"), false, &error));
    client.uploadPagesByChannel[QStringLiteral("UCAlpha")] = {FakeUploadPage{
        {},
        {QStringLiteral("video-UCAlpha")},
        QStringLiteral("token-two"),
        {}}};
    service.refresh();
    states = repository.channelHistoryStates(&error);
    QCOMPARE(states.first().nextPageToken, QStringLiteral("token-seven"));
}

void YouTubeTest::loadOlderFetchesNextUploadPages()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    FakeYouTubeClient client;
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCAlpha"),
        {
            FakeUploadPage{
                {},
                {QStringLiteral("video-UCAlpha")},
                QStringLiteral("token-two"),
                {}},
            FakeUploadPage{
                QStringLiteral("token-two"),
                {QStringLiteral("older-UCAlpha")},
                {},
                {}},
        });
    RefreshService service(&repository, &client);

    service.refresh();
    QCOMPARE(repository.feed().size(), 1);
    QVERIFY(!service.historyLoading());

    QString historyError = QStringLiteral("unset");
    bool finished = false;
    connect(&service, &RefreshService::historyFinished, this,
            [&](const QString &errorText) {
                finished = true;
                historyError = errorText;
            });
    service.loadOlder(std::nullopt);

    QVERIFY(finished);
    QVERIFY2(historyError.isEmpty(), qPrintable(historyError));
    QVERIFY(!service.historyLoading());
    QCOMPARE(client.lastRequestedPageToken, QStringLiteral("token-two"));
    QCOMPARE(repository.feed().size(), 2);
    const QList<ChannelHistoryState> states = repository.channelHistoryStates(&error);
    QCOMPARE(states.size(), 1);
    QVERIFY(states.first().historyComplete);
}

void YouTubeTest::loadOlderScopesToCategoryChannels()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const qint64 categoryId = repository.addCategory(QStringLiteral("News"), &error);
    QVERIFY2(categoryId > 0, qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCBeta"), QStringLiteral("Beta")), &error));
    QVERIFY(repository.setChannelCategories(QStringLiteral("UCAlpha"), {categoryId}, &error));

    FakeYouTubeClient client;
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCAlpha"),
        {
            FakeUploadPage{{}, {QStringLiteral("video-UCAlpha")}, QStringLiteral("alpha-two"), {}},
            FakeUploadPage{QStringLiteral("alpha-two"), {QStringLiteral("older-UCAlpha")}, {}, {}},
        });
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCBeta"),
        {FakeUploadPage{{}, {QStringLiteral("video-UCBeta")}, {}, {}}});
    RefreshService service(&repository, &client);
    service.refresh();

    const int requestsBefore = client.uploadPageRequestCount;
    QString historyError = QStringLiteral("unset");
    connect(&service, &RefreshService::historyFinished, this,
            [&](const QString &errorText) { historyError = errorText; });
    service.loadOlder(categoryId);

    QVERIFY2(historyError.isEmpty(), qPrintable(historyError));
    // Exactly one page fetch: only the category member needed more history.
    QCOMPARE(client.uploadPageRequestCount - requestsBefore, 1);
    QCOMPARE(client.lastRequestedPageToken, QStringLiteral("alpha-two"));
}

void YouTubeTest::loadOlderKeepsTokenOnFailure()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    FakeYouTubeClient client;
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCAlpha"),
        {
            FakeUploadPage{
                {},
                {QStringLiteral("video-UCAlpha")},
                QStringLiteral("token-two"),
                {}},
            FakeUploadPage{
                QStringLiteral("token-two"),
                {},
                {},
                QStringLiteral("backend error")},
        });
    RefreshService service(&repository, &client);
    service.refresh();

    QString historyError;
    connect(&service, &RefreshService::historyFinished, this,
            [&](const QString &errorText) { historyError = errorText; });
    service.loadOlder(std::nullopt);

    QVERIFY(historyError.contains(QStringLiteral("backend error")));
    // The stored token was not advanced, so the next attempt retries the
    // same page instead of skipping it.
    const QList<ChannelHistoryState> states = repository.channelHistoryStates(&error);
    QCOMPARE(states.first().nextPageToken, QStringLiteral("token-two"));
    QVERIFY(!states.first().historyComplete);
}

void YouTubeTest::loadOlderSkipsCompleteChannels()
{
    Repository repository(QStringLiteral(":memory:"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    QVERIFY(repository.upsertChannel(makeChannel(QStringLiteral("UCAlpha"), QStringLiteral("Alpha")), &error));
    FakeYouTubeClient client;
    client.uploadPagesByChannel.insert(
        QStringLiteral("UCAlpha"),
        {FakeUploadPage{{}, {QStringLiteral("video-UCAlpha")}, {}, {}}});
    RefreshService service(&repository, &client);
    service.refresh();

    const QList<ChannelHistoryState> states = repository.channelHistoryStates(&error);
    QVERIFY(states.first().historyComplete);

    const int requestsBefore = client.uploadPageRequestCount;
    QString historyError = QStringLiteral("unset");
    connect(&service, &RefreshService::historyFinished, this,
            [&](const QString &errorText) { historyError = errorText; });
    service.loadOlder(std::nullopt);

    QVERIFY2(historyError.isEmpty(), qPrintable(historyError));
    QCOMPARE(client.uploadPageRequestCount, requestsBefore);
}

void YouTubeTest::generatesLongFormFeedUrls()
{
    const QUrl url =
        longFormYouTubeFeedUrl(QStringLiteral("UC1234567890123456789012"));
    QCOMPARE(
        url.toString(),
        QStringLiteral(
            "https://www.youtube.com/feeds/videos.xml?playlist_id=UULF1234567890123456789012"));
    QCOMPARE(url.query(), QStringLiteral("playlist_id=UULF1234567890123456789012"));

    QVERIFY(!longFormYouTubeFeedUrl(QString()).isValid());
    QVERIFY(!longFormYouTubeFeedUrl(QStringLiteral("UUX1234567890123456789012")).isValid());
    QVERIFY(!longFormYouTubeFeedUrl(QStringLiteral("UC123456789012345678901")).isValid());
    QVERIFY(!longFormYouTubeFeedUrl(QStringLiteral("UC12345678901234567890123")).isValid());
    QVERIFY(!longFormYouTubeFeedUrl(QStringLiteral("UC12345678901234567890&2")).isValid());
}

void YouTubeTest::parsesAtomFeed()
{
    const QByteArray xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:yt="http://www.youtube.com/xml/schemas/2015" xmlns:media="http://search.yahoo.com/mrss/">
  <link rel="hub" href="https://pubsubhubbub.appspot.com"/>
  <title>Qt uploads</title>
  <id>yt:channel:UC1234567890123456789012</id>
  <author>
    <name>Qt</name>
    <uri>https://www.youtube.com/channel/UC1234567890123456789012</uri>
  </author>
  <yt:channelId>UC1234567890123456789012</yt:channelId>
  <entry>
    <id>yt:video:vid-one</id>
    <yt:videoId>vid-one</yt:videoId>
    <yt:channelId>UC1234567890123456789012</yt:channelId>
    <title>First video</title>
    <published>2026-08-30T10:00:00+00:00</published>
    <media:group>
      <media:title>First video</media:title>
    </media:group>
  </entry>
  <entry>
    <id>yt:video:vid-two</id>
    <yt:videoId>vid-two</yt:videoId>
    <yt:channelId>UC1234567890123456789012</yt:channelId>
    <title>Second video</title>
    <published>2026-08-29T09:30:00Z</published>
  </entry>
</feed>)";
    QString error = QStringLiteral("unset");
    const std::optional<YouTubeFeed> feed = parseYouTubeFeed(xml, &error);
    QVERIFY2(feed.has_value(), qPrintable(error));
    QCOMPARE(feed->channelId, QStringLiteral("UC1234567890123456789012"));
    QCOMPARE(feed->channelTitle, QStringLiteral("Qt"));
    QCOMPARE(feed->videos.size(), 2);
    QCOMPARE(feed->videos.first().id, QStringLiteral("vid-one"));
    QCOMPARE(feed->videos.first().channelId, QStringLiteral("UC1234567890123456789012"));
    QCOMPARE(feed->videos.first().channelTitle, QStringLiteral("Qt"));
    QCOMPARE(feed->videos.first().title, QStringLiteral("First video"));
    QCOMPARE(
        feed->videos.first().publishedAt.toString(Qt::ISODate),
        QStringLiteral("2026-08-30T10:00:00Z"));
    QVERIFY(!feed->videos.first().isBroadcast);
    QCOMPARE(feed->videos.first().broadcastState, QStringLiteral("none"));
    QCOMPARE(feed->videos.first().durationSeconds, -1);
    QVERIFY(feed->videos.first().fetchedAt.isValid());
    QCOMPARE(feed->videos.last().id, QStringLiteral("vid-two"));
    QCOMPARE(
        feed->videos.last().publishedAt.toString(Qt::ISODate),
        QStringLiteral("2026-08-29T09:30:00Z"));

    // An empty feed is valid when feed-level channel metadata is present,
    // and the feed title is the fallback when no author name exists.
    const QByteArray emptyFeed = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:yt="http://www.youtube.com/xml/schemas/2015">
  <title>Qt uploads</title>
  <yt:channelId>UC1234567890123456789012</yt:channelId>
</feed>)";
    error.clear();
    const std::optional<YouTubeFeed> empty = parseYouTubeFeed(emptyFeed, &error);
    QVERIFY2(empty.has_value(), qPrintable(error));
    QCOMPARE(empty->videos.size(), 0);
    QCOMPARE(empty->channelTitle, QStringLiteral("Qt uploads"));
}

void YouTubeTest::ignoresMalformedFeedEntries()
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:yt="http://www.youtube.com/xml/schemas/2015">
  <title>Qt uploads</title>
  <yt:channelId>UC1234567890123456789012</yt:channelId>
  <entry>
    <yt:videoId>missing-channel</yt:videoId>
    <title>No channel id</title>
    <published>2026-08-30T10:00:00Z</published>
  </entry>
  <entry>
    <yt:videoId>missing-timestamp</yt:videoId>
    <yt:channelId>UC1234567890123456789012</yt:channelId>
    <title>No timestamp</title>
    <published>not-a-date</published>
  </entry>
  <entry>
    <yt:videoId>good</yt:videoId>
    <yt:channelId>UC1234567890123456789012</yt:channelId>
    <title>Good video</title>
    <published>2026-08-30T11:00:00Z</published>
  </entry>
</feed>)";
    QString error = QStringLiteral("unset");
    const std::optional<YouTubeFeed> feed = parseYouTubeFeed(xml, &error);
    QVERIFY2(feed.has_value(), qPrintable(error));
    QCOMPARE(feed->videos.size(), 1);
    QCOMPARE(feed->videos.first().id, QStringLiteral("good"));
    QCOMPARE(feed->videos.first().title, QStringLiteral("Good video"));
}

void YouTubeTest::rejectsInvalidFeedXml()
{
    QString error;
    QVERIFY(!parseYouTubeFeed(QByteArray(), &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!parseYouTubeFeed(QByteArray("<feed><title>truncated"), &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!parseYouTubeFeed(QByteArray("<rss/>"), &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!parseYouTubeFeed(QByteArray("not xml at all"), &error));
    QVERIFY(!error.isEmpty());

    // Entry-level title and channelId must not satisfy feed metadata.
    error.clear();
    const QByteArray noFeedMetadata = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:yt="http://www.youtube.com/xml/schemas/2015">
  <entry>
    <yt:videoId>vid</yt:videoId>
    <yt:channelId>UC1234567890123456789012</yt:channelId>
    <title>Only entry title</title>
    <published>2026-08-30T10:00:00Z</published>
  </entry>
</feed>)";
    QVERIFY(!parseYouTubeFeed(noFeedMetadata, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    const QByteArray noChannelId = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Qt uploads</title>
</feed>)";
    QVERIFY(!parseYouTubeFeed(noChannelId, &error));
    QVERIFY(!error.isEmpty());
}

void YouTubeTest::parsesYtDlpResponses()
{
    const QByteArray channelJson = R"({"id":"UC1234567890123456789012","channel":"Qt","uploader_id":"@QtGroup","thumbnails":[{"url":"small"},{"url":"large"}],"entries":[]})";
    QString error;
    const std::optional<Channel> channel = YouTubeClient::parseYtDlpChannel(
        channelJson,
        QStringLiteral("@QtGroup"),
        &error);
    QVERIFY2(channel.has_value(), qPrintable(error));
    QCOMPARE(channel->id, QStringLiteral("UC1234567890123456789012"));
    QCOMPARE(channel->title, QStringLiteral("Qt"));
    QCOMPARE(channel->handle, QStringLiteral("@QtGroup"));
    QCOMPARE(channel->avatarUrl, QStringLiteral("large"));
    QCOMPARE(channel->uploadsPlaylistId, QStringLiteral("UU1234567890123456789012"));

    const QByteArray videosJson =
        R"({"id":"regular-id","title":"Regular","channel_id":"UC1234567890123456789012","channel":"Qt","timestamp":1788170400,"duration":601,"live_status":"not_live"})"
        "\n"
        R"({"id":"stream-id","title":"Stream","channel_id":"UC1234567890123456789012","channel":"Qt","upload_date":"20260830","duration":3600,"live_status":"was_live"})";
    const QList<Video> videos = YouTubeClient::parseYtDlpVideos(videosJson, *channel, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(videos.size(), 2);
    QCOMPARE(videos.first().durationSeconds, 601);
    QVERIFY(!videos.first().isBroadcast);
    QVERIFY(videos.last().isBroadcast);

    const QByteArray liveJson =
        R"({"id":"scheduled","title":"Later","live_status":"is_upcoming"})"
        "\n"
        R"({"id":"live-now","title":"Live now","live_status":"is_live"})";
    const std::optional<LiveChannel> live =
        YouTubeClient::parseYtDlpLive(liveJson, *channel, &error);
    QVERIFY2(live.has_value(), qPrintable(error));
    QCOMPARE(live->videoId, QStringLiteral("live-now"));
}

QTEST_GUILESS_MAIN(YouTubeTest)

#include "youtube_test.moc"
