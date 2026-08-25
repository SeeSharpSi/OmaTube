#include "refreshservice.h"
#include "repository.h"
#include "youtubeclient.h"

#include <QSignalSpy>
#include <QTest>

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

    void fetchUploadVideoIds(const Channel &channel, VideoIdsCallback callback) override
    {
        callback({QStringLiteral("video-%1").arg(channel.id)}, {});
    }

    void fetchVideos(const QStringList &videoIds, VideosCallback callback) override
    {
        QList<Video> videos;
        for (const QString &id : videoIds) {
            const QString channelId = id.mid(QStringLiteral("video-").size());
            videos.append({
                id,
                channelId,
                {},
                QStringLiteral("Video from %1").arg(channelId),
                QDateTime::currentDateTimeUtc(),
                false,
                QStringLiteral("none"),
                QDateTime::currentDateTimeUtc(),
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
    void refreshServiceRefreshesStaleChannelMetadata();
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

    const QByteArray videosJson = R"({"items":[{"id":"regular","snippet":{"channelId":"UC1234567890123456789012","channelTitle":"Qt","title":"Regular","publishedAt":"2026-08-24T12:00:00Z","liveBroadcastContent":"none"}},{"id":"stream","snippet":{"channelId":"UC1234567890123456789012","channelTitle":"Qt","title":"Stream","publishedAt":"2026-08-24T13:00:00Z","liveBroadcastContent":"live"},"liveStreamingDetails":{"actualStartTime":"2026-08-24T13:00:00Z"}}]})";
    const QList<Video> videos = YouTubeClient::parseVideosResponse(videosJson, &error);
    QCOMPARE(videos.size(), 2);
    QVERIFY(!videos.first().isBroadcast);
    QVERIFY(videos.last().isBroadcast);

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

QTEST_GUILESS_MAIN(YouTubeTest)

#include "youtube_test.moc"
