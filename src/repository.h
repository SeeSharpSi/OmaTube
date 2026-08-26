#pragma once

#include "domain.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

class Repository
{
public:
    // Cached history grows without age limits until the database reaches this
    // size; then the oldest videos are pruned first in, first out.
    static constexpr qint64 maximumDatabaseBytes = 10 * 1024 * 1024;
    // Fetching deeper history is only allowed below this soft limit so a
    // prune pass is not immediately undone by the next page load.
    static constexpr qint64 historyFetchDatabaseBytes = 9 * 1024 * 1024;

    explicit Repository(QString databasePath = {});
    ~Repository();

    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    bool open(QString *error = nullptr);

    [[nodiscard]] QList<Category> categories(QString *error = nullptr) const;
    qint64 addCategory(const QString &name, QString *error = nullptr);
    bool renameCategory(qint64 id, const QString &name, QString *error = nullptr);
    bool removeCategory(qint64 id, QString *error = nullptr);

    [[nodiscard]] QList<Channel> channels(
        std::optional<qint64> categoryId = std::nullopt,
        QString *error = nullptr) const;
    bool upsertChannel(const Channel &channel, QString *error = nullptr);
    bool removeChannel(const QString &channelId, QString *error = nullptr);
    bool setChannelCategories(
        const QString &channelId,
        const QList<qint64> &categoryIds,
        QString *error = nullptr);
    bool setChannelCategoryMembership(
        const QString &channelId,
        qint64 categoryId,
        bool member,
        QString *error = nullptr);
    [[nodiscard]] QList<qint64> categoryIdsForChannel(
        const QString &channelId,
        QString *error = nullptr) const;

    bool upsertVideos(const QList<Video> &videos, QString *error = nullptr);
    [[nodiscard]] std::optional<Video> video(const QString &videoId, QString *error = nullptr) const;
    [[nodiscard]] QList<Video> feed(
        std::optional<qint64> categoryId = std::nullopt,
        int shortVideoCutoffSeconds = 180,
        int limit = 500,
        QString *error = nullptr) const;
    // Page of the feed strictly older than the (publishedBefore, idBefore)
    // cursor; an invalid cursor pages from the newest video.
    [[nodiscard]] QList<Video> feedPage(
        std::optional<qint64> categoryId,
        int shortVideoCutoffSeconds,
        const QDateTime &publishedBefore,
        const QString &idBefore,
        int limit,
        QString *error = nullptr) const;

    [[nodiscard]] QList<ChannelHistoryState> channelHistoryStates(QString *error = nullptr) const;
    // Records where a channel's history fetching should resume. Existing
    // rows are left untouched so a regular refresh never rewinds a cursor
    // that has already been advanced by deeper history loads.
    bool initializeChannelHistory(
        const QString &channelId,
        const QString &nextPageToken,
        QString *error = nullptr);
    bool setChannelHistoryState(
        const QString &channelId,
        const QString &nextPageToken,
        bool historyComplete,
        QString *error = nullptr);
    // True when at least one channel in scope has uploads history that has
    // not been fetched to exhaustion yet.
    [[nodiscard]] bool historyIncomplete(
        std::optional<qint64> categoryId = std::nullopt,
        QString *error = nullptr) const;

    [[nodiscard]] qint64 databaseSizeBytes() const;
    [[nodiscard]] bool canFetchMoreHistory() const;
    bool pruneVideoMetadataToLimit(qint64 maximumBytes, QString *error = nullptr);

    [[nodiscard]] std::optional<WatchStats> watchStats(
        const QString &videoId,
        QString *error = nullptr) const;
    bool applyWatchProgress(
        const QString &videoId,
        qint64 watchedSecondsDelta,
        int lastPositionSeconds,
        bool countSession,
        QString *error = nullptr);

private:
    bool migrate(QString *error);
    static void setError(QString *target, const QString &message);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
};
