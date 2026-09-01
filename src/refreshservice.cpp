#include "refreshservice.h"

#include "repository.h"
#include "youtubeclient.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(refreshLog, "omatube.refresh", QtWarningMsg)

RefreshService::RefreshService(
    Repository *repository,
    YouTubeClient *youTubeClient,
    QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_youTubeClient(youTubeClient)
{
}

bool RefreshService::refreshing() const
{
    return m_refreshing;
}

QString RefreshService::progressText() const
{
    return m_progressText;
}

void RefreshService::refresh()
{
    if (m_refreshing || m_historyLoading)
        return;

    m_refreshing = true;
    qCDebug(refreshLog) << "starting feed refresh";
    emit refreshingChanged();
    m_channels.clear();
    m_metadataQueue.clear();
    m_uploadQueue.clear();
    m_videoQueue.clear();
    m_liveQueue.clear();
    m_videoIds.clear();
    m_feedErrors.clear();
    m_liveErrors.clear();
    m_metadataInFlight = 0;
    m_uploadsInFlight = 0;
    m_videosInFlight = 0;
    m_liveInFlight = 0;

    QString error;
    m_channels = m_repository->channels(std::nullopt, &error);
    qCDebug(refreshLog) << "loaded" << m_channels.size() << "channels";
    if (!error.isEmpty()) {
        m_feedErrors.append(error);
        m_liveErrors.append(error);
        finish();
        return;
    }
    if (m_channels.isEmpty()) {
        m_liveChannels.clear();
        emit liveChannelsChanged({});
        finish();
        return;
    }
    QSet<QString> channelIds;
    for (const Channel &channel : std::as_const(m_channels))
        channelIds.insert(channel.id);
    m_liveChannels.removeIf([&channelIds](const LiveChannel &live) {
        return !channelIds.contains(live.channelId);
    });
    beginMetadataRefresh();
}

bool RefreshService::historyLoading() const
{
    return m_historyLoading;
}

void RefreshService::loadOlder(std::optional<qint64> categoryId)
{
    if (m_refreshing || m_historyLoading)
        return;
    qCDebug(refreshLog) << "loading older videos for category" << categoryId;
    QString error;
    const QList<Channel> channels = m_repository->channels(categoryId, &error);
    if (!error.isEmpty()) {
        emit historyFinished(error);
        return;
    }

    QHash<QString, ChannelHistoryState> states;
    for (const ChannelHistoryState &state : m_repository->channelHistoryStates(&error)) {
        if (!error.isEmpty()) {
            emit historyFinished(error);
            return;
        }
        states.insert(state.channelId, state);
    }

    m_historyQueue.clear();
    m_historyVideoQueue.clear();
    m_historyPageTokens.clear();
    m_historyVideoIds.clear();
    m_historyErrors.clear();
    for (const Channel &channel : channels) {
        const auto state = states.constFind(channel.id);
        if (state != states.constEnd()) {
            if (state->historyComplete)
                continue;
            m_historyPageTokens.insert(channel.id, state->nextPageToken);
        }
        m_historyQueue.enqueue(channel);
    }

    if (m_historyQueue.isEmpty()) {
        emit historyFinished({});
        return;
    }

    m_historyLoading = true;
    emit historyLoadingChanged();
    dispatchHistoryPages();
}

void RefreshService::dispatchHistoryPages()
{
    while (m_historyInFlight < maximumConcurrentRequests && !m_historyQueue.isEmpty()) {
        const Channel channel = m_historyQueue.dequeue();
        ++m_historyInFlight;
        m_youTubeClient->fetchUploadPage(
            channel,
            m_historyPageTokens.value(channel.id),
            [this, channel](UploadPage page, QString error) {
                --m_historyInFlight;
                if (!error.isEmpty()) {
                    m_historyErrors.append(
                        QStringLiteral("%1: %2").arg(channel.title, error));
                } else {
                    QString databaseError;
                    if (!page.videos.isEmpty()
                        && !m_repository->upsertVideos(page.videos, &databaseError)) {
                        m_historyErrors.append(databaseError);
                    }
                    for (const QString &id : page.videoIds)
                        m_historyVideoIds.insert(id);
                    if (!m_repository->setChannelHistoryState(
                            channel.id,
                            page.nextPageToken,
                            page.nextPageToken.isEmpty(),
                            &databaseError)) {
                        m_historyErrors.append(databaseError);
                    }
                }
                if (m_historyQueue.isEmpty() && m_historyInFlight == 0)
                    beginHistoryVideoDetails();
                else
                    dispatchHistoryPages();
            });
    }
}

void RefreshService::beginHistoryVideoDetails()
{
    QStringList ids(m_historyVideoIds.cbegin(), m_historyVideoIds.cend());
    while (!ids.isEmpty()) {
        QStringList chunk;
        const int chunkSize = qMin(50, ids.size());
        chunk.reserve(chunkSize);
        for (int i = 0; i < chunkSize; ++i)
            chunk.append(ids.takeLast());
        m_historyVideoQueue.enqueue(chunk);
    }

    if (m_historyVideoQueue.isEmpty()) {
        finishHistoryLoad();
        return;
    }
    dispatchHistoryVideoDetails();
}

void RefreshService::dispatchHistoryVideoDetails()
{
    while (m_historyVideosInFlight < maximumConcurrentRequests && !m_historyVideoQueue.isEmpty()) {
        const QStringList ids = m_historyVideoQueue.dequeue();
        ++m_historyVideosInFlight;
        m_youTubeClient->fetchVideos(ids, [this](QList<Video> videos, QString error) {
            --m_historyVideosInFlight;
            if (!error.isEmpty()) {
                m_historyErrors.append(error);
            } else {
                QString databaseError;
                if (!m_repository->upsertVideos(videos, &databaseError))
                    m_historyErrors.append(databaseError);
            }
            if (m_historyVideoQueue.isEmpty() && m_historyVideosInFlight == 0)
                finishHistoryLoad();
            else
                dispatchHistoryVideoDetails();
        });
    }
}

void RefreshService::finishHistoryLoad()
{
    m_historyLoading = false;
    emit historyLoadingChanged();
    emit historyFinished(summarizeErrors(m_historyErrors));
}

void RefreshService::beginMetadataRefresh()
{
    const QDateTime refreshBefore = QDateTime::currentDateTimeUtc().addDays(-29);
    if (m_youTubeClient->hasApiKey()) {
        for (const Channel &channel : std::as_const(m_channels)) {
            if (!channel.metadataFetchedAt.isValid() || channel.metadataFetchedAt < refreshBefore)
                m_metadataQueue.enqueue(channel);
        }
    }

    m_stageCompleted = 0;
    m_stageTotal = m_metadataQueue.size();
    qCDebug(refreshLog) << "metadata refresh count" << m_stageTotal;
    if (m_stageTotal == 0) {
        beginUploads();
        return;
    }
    setProgressText(QStringLiteral("Refreshing channel details 0/%1").arg(m_stageTotal));
    dispatchMetadataRefresh();
}

void RefreshService::dispatchMetadataRefresh()
{
    while (m_metadataInFlight < maximumConcurrentRequests && !m_metadataQueue.isEmpty()) {
        const Channel previous = m_metadataQueue.dequeue();
        ++m_metadataInFlight;
        m_youTubeClient->resolveChannel(
            previous.id,
            [this, previous](std::optional<Channel> refreshed, QString error) {
                --m_metadataInFlight;
                ++m_stageCompleted;
                if (!error.isEmpty() || !refreshed) {
                    m_feedErrors.append(
                        QStringLiteral("%1 metadata: %2")
                            .arg(previous.title, error.isEmpty() ? QStringLiteral("not found") : error));
                } else {
                    refreshed->originalInput = previous.originalInput;
                    QString databaseError;
                    if (!m_repository->upsertChannel(*refreshed, &databaseError)) {
                        m_feedErrors.append(databaseError);
                    } else {
                        const auto item = std::find_if(
                            m_channels.begin(),
                            m_channels.end(),
                            [&previous](const Channel &channel) { return channel.id == previous.id; });
                        if (item != m_channels.end())
                            *item = std::move(*refreshed);
                    }
                }
                setProgressText(
                    QStringLiteral("Refreshing channel details %1/%2")
                        .arg(m_stageCompleted)
                        .arg(m_stageTotal));
                if (m_metadataQueue.isEmpty() && m_metadataInFlight == 0)
                    beginUploads();
                else
                    dispatchMetadataRefresh();
            });
    }
}

void RefreshService::beginUploads()
{
    for (const Channel &channel : std::as_const(m_channels))
        m_uploadQueue.enqueue(channel);
    m_stageCompleted = 0;
    m_stageTotal = m_uploadQueue.size();
    qCDebug(refreshLog) << "upload feed count" << m_stageTotal;
    setProgressText(QStringLiteral("Loading uploads 0/%1").arg(m_stageTotal));
    dispatchUploads();
}

void RefreshService::dispatchUploads()
{
    while (m_uploadsInFlight < maximumConcurrentRequests && !m_uploadQueue.isEmpty()) {
        const Channel channel = m_uploadQueue.dequeue();
        ++m_uploadsInFlight;
        m_youTubeClient->fetchUploadPage(
            channel,
            {},
            [this, channel](UploadPage page, QString error) {
                --m_uploadsInFlight;
                ++m_stageCompleted;
                if (!error.isEmpty()) {
                    m_feedErrors.append(QStringLiteral("%1: %2").arg(channel.title, error));
                } else {
                    if (!page.videos.isEmpty())
                        storeSourceVideos(channel, page.videos);
                    for (const QString &id : page.videoIds)
                        m_videoIds.insert(id);
                    QString databaseError;
                    if (!m_repository->initializeChannelHistory(
                            channel.id,
                            page.nextPageToken,
                            &databaseError)) {
                        m_feedErrors.append(databaseError);
                    }
                }
                setProgressText(
                    QStringLiteral("Loading uploads %1/%2").arg(m_stageCompleted).arg(m_stageTotal));
                if (m_uploadQueue.isEmpty() && m_uploadsInFlight == 0)
                    beginVideoDetails();
                else
                    dispatchUploads();
            });
    }
}

void RefreshService::beginVideoDetails()
{
    QStringList ids(m_videoIds.cbegin(), m_videoIds.cend());
    while (!ids.isEmpty()) {
        QStringList chunk;
        const int chunkSize = qMin(50, ids.size());
        chunk.reserve(chunkSize);
        for (int i = 0; i < chunkSize; ++i)
            chunk.append(ids.takeLast());
        m_videoQueue.enqueue(chunk);
    }

    m_stageCompleted = 0;
    m_stageTotal = m_videoQueue.size();
    qCDebug(refreshLog) << "video detail batch count" << m_stageTotal;
    if (m_stageTotal == 0) {
        beginLiveChecks();
        return;
    }
    setProgressText(QStringLiteral("Loading video details 0/%1").arg(m_stageTotal));
    dispatchVideoDetails();
}

void RefreshService::dispatchVideoDetails()
{
    while (m_videosInFlight < maximumConcurrentRequests && !m_videoQueue.isEmpty()) {
        const QStringList ids = m_videoQueue.dequeue();
        ++m_videosInFlight;
        m_youTubeClient->fetchVideos(ids, [this](QList<Video> videos, QString error) {
            --m_videosInFlight;
            ++m_stageCompleted;
            if (!error.isEmpty()) {
                m_feedErrors.append(error);
            } else {
                QString databaseError;
                if (!m_repository->upsertVideos(videos, &databaseError))
                    m_feedErrors.append(databaseError);
            }
            setProgressText(
                QStringLiteral("Loading video details %1/%2").arg(m_stageCompleted).arg(m_stageTotal));
            if (m_videoQueue.isEmpty() && m_videosInFlight == 0) {
                emit feedChanged();
                beginLiveChecks();
            } else {
                dispatchVideoDetails();
            }
        });
    }
}

void RefreshService::beginLiveChecks()
{
    for (const Channel &channel : std::as_const(m_channels))
        m_liveQueue.enqueue(channel);
    m_stageCompleted = 0;
    m_stageTotal = m_liveQueue.size();
    qCDebug(refreshLog) << "live check count" << m_stageTotal;
    setProgressText(QStringLiteral("Checking live channels 0/%1").arg(m_stageTotal));
    dispatchLiveChecks();
}

void RefreshService::dispatchLiveChecks()
{
    while (m_liveInFlight < maximumConcurrentRequests && !m_liveQueue.isEmpty()) {
        const Channel channel = m_liveQueue.dequeue();
        ++m_liveInFlight;
        m_youTubeClient->fetchLiveChannel(
            channel,
            [this, channel](std::optional<LiveChannel> live, QString error) {
                --m_liveInFlight;
                ++m_stageCompleted;
                if (!error.isEmpty())
                    m_liveErrors.append(QStringLiteral("%1: %2").arg(channel.title, error));
                else
                    updateLiveChannel(channel, std::move(live));
                setProgressText(
                    QStringLiteral("Checking live channels %1/%2").arg(m_stageCompleted).arg(m_stageTotal));
                if (m_liveQueue.isEmpty() && m_liveInFlight == 0)
                    finish();
                else
                    dispatchLiveChecks();
            });
    }
}

void RefreshService::storeSourceVideos(const Channel &channel, const QList<Video> &videos)
{
    QString error;
    if (!m_repository->upsertVideos(videos, &error)) {
        m_feedErrors.append(error);
        return;
    }
    emit feedChanged();
    m_youTubeClient->enrichVideos(
        channel,
        videos,
        [this](QList<Video> enriched, QString enrichmentError) {
            if (!enrichmentError.isEmpty() || enriched.isEmpty())
                return;
            QString databaseError;
            if (m_repository->upsertVideos(enriched, &databaseError))
                emit feedChanged();
        });
}

void RefreshService::updateLiveChannel(
    const Channel &channel,
    std::optional<LiveChannel> live)
{
    const auto existing = std::find_if(
        m_liveChannels.begin(),
        m_liveChannels.end(),
        [&channel](const LiveChannel &candidate) { return candidate.channelId == channel.id; });
    if (live) {
        if (existing == m_liveChannels.end())
            m_liveChannels.append(std::move(*live));
        else
            *existing = std::move(*live);
    } else if (existing != m_liveChannels.end()) {
        m_liveChannels.erase(existing);
    }
    std::sort(
        m_liveChannels.begin(),
        m_liveChannels.end(),
        [](const LiveChannel &left, const LiveChannel &right) {
            return left.channelTitle.localeAwareCompare(right.channelTitle) < 0;
        });
    emit liveChannelsChanged(m_liveChannels);
}

void RefreshService::finish()
{
    QString pruneError;
    if (!m_repository->pruneVideoMetadataToLimit(Repository::maximumDatabaseBytes, &pruneError))
        m_feedErrors.append(pruneError);

    m_refreshing = false;
    qCDebug(refreshLog) << "refresh finished with" << m_feedErrors.size() << "feed errors and"
                        << m_liveErrors.size() << "live errors";
    std::sort(
        m_liveChannels.begin(),
        m_liveChannels.end(),
        [](const LiveChannel &left, const LiveChannel &right) {
            return left.channelTitle.localeAwareCompare(right.channelTitle) < 0;
        });
    setProgressText({});
    emit refreshingChanged();
    emit refreshFinished(
        m_liveChannels,
        m_liveErrors.isEmpty(),
        summarizeErrors(m_feedErrors),
        summarizeErrors(m_liveErrors));
}

void RefreshService::setProgressText(QString text)
{
    if (m_progressText == text)
        return;
    m_progressText = std::move(text);
    emit progressTextChanged();
}

QString RefreshService::summarizeErrors(const QStringList &errors)
{
    if (errors.isEmpty())
        return {};
    QStringList shown = errors.sliced(0, qMin(3, errors.size()));
    if (errors.size() > shown.size())
        shown.append(QStringLiteral("%1 more errors").arg(errors.size() - shown.size()));
    return shown.join(QStringLiteral("\n"));
}
