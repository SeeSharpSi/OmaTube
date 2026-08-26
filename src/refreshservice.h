#pragma once

#include "domain.h"

#include <QHash>
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
    [[nodiscard]] bool historyLoading() const;

    void refresh();
    // Fetches one further uploads page for every channel in scope that has
    // not been fetched to exhaustion yet, then stores the video details.
    void loadOlder(std::optional<qint64> categoryId);

signals:
    void refreshingChanged();
    void progressTextChanged();
    void feedChanged();
    void historyLoadingChanged();
    void historyFinished(QString error);
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
    void dispatchHistoryPages();
    void beginHistoryVideoDetails();
    void dispatchHistoryVideoDetails();
    void finishHistoryLoad();
    void setProgressText(QString text);
    static QString summarizeErrors(const QStringList &errors);

    Repository *m_repository;
    YouTubeClient *m_youTubeClient;
    bool m_refreshing = false;
    bool m_historyLoading = false;
    QString m_progressText;
    QList<Channel> m_channels;
    QQueue<Channel> m_metadataQueue;
    QQueue<Channel> m_uploadQueue;
    QQueue<QStringList> m_videoQueue;
    QQueue<Channel> m_liveQueue;
    QQueue<Channel> m_historyQueue;
    QQueue<QStringList> m_historyVideoQueue;
    QHash<QString, QString> m_historyPageTokens;
    int m_metadataInFlight = 0;
    int m_uploadsInFlight = 0;
    int m_videosInFlight = 0;
    int m_liveInFlight = 0;
    int m_historyInFlight = 0;
    int m_historyVideosInFlight = 0;
    int m_stageCompleted = 0;
    int m_stageTotal = 0;
    QSet<QString> m_videoIds;
    QSet<QString> m_historyVideoIds;
    QList<LiveChannel> m_liveChannels;
    QStringList m_feedErrors;
    QStringList m_liveErrors;
    QStringList m_historyErrors;
};
