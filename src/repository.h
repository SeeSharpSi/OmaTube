#pragma once

#include "domain.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

class Repository
{
public:
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
    [[nodiscard]] QList<Video> feed(
        std::optional<qint64> categoryId = std::nullopt,
        int limit = 500,
        QString *error = nullptr) const;
    bool pruneVideoMetadata(const QDateTime &olderThan, QString *error = nullptr);

private:
    bool migrate(QString *error);
    static void setError(QString *target, const QString &message);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
};
