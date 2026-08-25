#pragma once

#include "domain.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
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
    using VideoIdsCallback = std::function<void(QStringList, QString)>;
    using VideosCallback = std::function<void(QList<Video>, QString)>;
    using LiveCallback = std::function<void(std::optional<LiveChannel>, QString)>;

    explicit YouTubeClient(QObject *parent = nullptr);
    ~YouTubeClient() override = default;

    void setApiKey(QString apiKey);
    [[nodiscard]] virtual bool hasApiKey() const;

    virtual void resolveChannel(const QString &input, ResolveCallback callback);
    virtual void fetchUploadVideoIds(const Channel &channel, VideoIdsCallback callback);
    virtual void fetchVideos(const QStringList &videoIds, VideosCallback callback);
    virtual void fetchLiveChannel(const Channel &channel, LiveCallback callback);

    static std::optional<ChannelReference> parseChannelReference(
        const QString &input,
        QString *error = nullptr);
    static std::optional<Channel> parseChannelResponse(
        const QByteArray &json,
        const QString &originalInput,
        QString *error = nullptr);
    static QStringList parseUploadVideoIds(const QByteArray &json, QString *error = nullptr);
    static QList<Video> parseVideosResponse(const QByteArray &json, QString *error = nullptr);
    static std::optional<LiveChannel> parseLiveResponse(
        const QByteArray &json,
        const Channel &channel,
        QString *error = nullptr);

private:
    using JsonCallback = std::function<void(QByteArray, QString)>;

    void getJson(QUrl url, JsonCallback callback);
    [[nodiscard]] QUrl apiUrl(const QString &path, const QList<QPair<QString, QString>> &items) const;
    static QString apiErrorMessage(const QByteArray &json);
    static void setError(QString *target, const QString &message);

    QString m_apiKey;
    QNetworkAccessManager m_network;
};
