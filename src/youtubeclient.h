#pragma once

#include "domain.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QStringList>

#include <functional>
#include <optional>

enum class ChannelReferenceKind {
    Handle,
    Id,
};

struct ChannelReference
{
    ChannelReferenceKind kind = ChannelReferenceKind::Handle;
    QString value;
};

class YouTubeClient : public QObject
{
    Q_OBJECT

public:
    using ResolveCallback = std::function<void(std::optional<Channel>, QString)>;
    using UploadPageCallback = std::function<void(UploadPage, QString)>;
    using VideosCallback = std::function<void(QList<Video>, QString)>;
    using LiveCallback = std::function<void(std::optional<LiveChannel>, QString)>;

    explicit YouTubeClient(QObject *parent = nullptr);
    ~YouTubeClient() override = default;

    void setApiKey(QString apiKey);
    [[nodiscard]] virtual bool hasApiKey() const;
    [[nodiscard]] virtual bool canFetchHistory() const;

    virtual void resolveChannel(const QString &input, ResolveCallback callback);
    virtual void fetchUploadPage(
        const Channel &channel,
        const QString &pageToken,
        UploadPageCallback callback);
    virtual void fetchVideos(const QStringList &videoIds, VideosCallback callback);
    virtual void fetchLiveChannel(const Channel &channel, LiveCallback callback);
    virtual void enrichVideos(
        const Channel &channel,
        const QList<Video> &videos,
        VideosCallback callback);

    static std::optional<ChannelReference> parseChannelReference(
        const QString &input,
        QString *error = nullptr);
    static std::optional<Channel> parseChannelResponse(
        const QByteArray &json,
        const QString &originalInput,
        QString *error = nullptr);
    static UploadPage parseUploadPage(const QByteArray &json, QString *error = nullptr);
    static QStringList parseUploadVideoIds(const QByteArray &json, QString *error = nullptr);
    static QList<Video> parseVideosResponse(const QByteArray &json, QString *error = nullptr);
    static std::optional<LiveChannel> parseLiveResponse(
        const QByteArray &json,
        const Channel &channel,
        QString *error = nullptr);
    static std::optional<Channel> parseYtDlpChannel(
        const QByteArray &json,
        const QString &originalInput,
        QString *error = nullptr);
    static QList<Video> parseYtDlpVideos(
        const QByteArray &json,
        const Channel &channel,
        QString *error = nullptr);
    static std::optional<LiveChannel> parseYtDlpLive(
        const QByteArray &json,
        const Channel &channel,
        QString *error = nullptr);

private:
    using DataCallback = std::function<void(QByteArray, QString)>;

    struct YtDlpJob
    {
        QStringList arguments;
        int timeoutMs = 30000;
        DataCallback callback;
    };

    void getData(QUrl url, DataCallback callback);
    void getJson(QUrl url, DataCallback callback);
    void resolveChannelKeyless(
        const QString &input,
        const ChannelReference &reference,
        ResolveCallback callback);
    void resolveChannelWithYtDlp(const QString &input, ResolveCallback callback);
    void fetchFeed(const Channel &channel, UploadPageCallback callback);
    void fetchYtDlpUploadPage(
        const Channel &channel,
        int startIndex,
        bool flat,
        UploadPageCallback callback);
    void runYtDlp(
        QStringList arguments,
        int timeoutMs,
        bool highPriority,
        DataCallback callback);
    void dispatchYtDlp();
    [[nodiscard]] QUrl channelTabUrl(const Channel &channel, const QString &tab) const;
    [[nodiscard]] QUrl apiUrl(const QString &path, const QList<QPair<QString, QString>> &items) const;
    static QString apiErrorMessage(const QByteArray &json);
    static void setError(QString *target, const QString &message);

    QString m_apiKey;
    QString m_ytDlpExecutable;
    QNetworkAccessManager m_network;
    QQueue<YtDlpJob> m_ytDlpQueue;
    int m_ytDlpInFlight = 0;
};
