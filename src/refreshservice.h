#pragma once

#include "domain.h"

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>

class Repository;
class YouTubeClient;

class RefreshService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressTextChanged)

public:
    explicit RefreshService(
        Repository *repository,
        YouTubeClient *youTubeClient,
        QObject *parent = nullptr);

    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] QString progressText() const;

    void refresh();

signals:
    void refreshingChanged();
    void progressTextChanged();
    void feedChanged();
    void refreshFinished(
        QList<LiveChannel> liveChannels,
        bool liveStatusComplete,
        QString feedError,
        QString liveError);

private:
    static constexpr int maximumConcurrentRequests = 4;

    void beginMetadataRefresh();
    void dispatchMetadataRefresh();
    void beginUploads();
    void dispatchUploads();
    void beginVideoDetails();
    void dispatchVideoDetails();
    void beginLiveChecks();
    void dispatchLiveChecks();
    void finish();
    void setProgressText(QString text);
    static QString summarizeErrors(const QStringList &errors);

    Repository *m_repository;
    YouTubeClient *m_youTubeClient;
    bool m_refreshing = false;
    QString m_progressText;
    QList<Channel> m_channels;
    QQueue<Channel> m_metadataQueue;
    QQueue<Channel> m_uploadQueue;
    QQueue<QStringList> m_videoQueue;
    QQueue<Channel> m_liveQueue;
    int m_metadataInFlight = 0;
    int m_uploadsInFlight = 0;
    int m_videosInFlight = 0;
    int m_liveInFlight = 0;
    int m_stageCompleted = 0;
    int m_stageTotal = 0;
    QSet<QString> m_videoIds;
    QList<LiveChannel> m_liveChannels;
    QStringList m_feedErrors;
    QStringList m_liveErrors;
};
