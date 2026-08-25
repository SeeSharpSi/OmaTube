#pragma once

#include "models/categorymodel.h"
#include "models/channelmodel.h"
#include "models/feedmodel.h"
#include "models/livechannelmodel.h"
#include "refreshservice.h"
#include "repository.h"
#include "youtubeclient.h"

#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

#include <memory>

class QQmlEngine;
class QJSEngine;

class AppController final : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON
    Q_PROPERTY(CategoryModel *categories READ categories CONSTANT)
    Q_PROPERTY(ChannelModel *channels READ channels CONSTANT)
    Q_PROPERTY(FeedModel *feed READ feed CONSTANT)
    Q_PROPERTY(LiveChannelModel *liveChannels READ liveChannels CONSTANT)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(bool addingChannel READ addingChannel NOTIFY addingChannelChanged)
    Q_PROPERTY(bool apiKeyConfigured READ apiKeyConfigured NOTIFY apiKeyConfiguredChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressTextChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QDateTime lastRefreshedAt READ lastRefreshedAt NOTIFY lastRefreshedAtChanged)
    Q_PROPERTY(qint64 selectedCategoryId READ selectedCategoryId NOTIFY selectedCategoryIdChanged)

public:
    ~AppController() override;

    static std::unique_ptr<AppController> createApplication(QString databasePath = {});
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool initialize(QString *error = nullptr);

    [[nodiscard]] CategoryModel *categories();
    [[nodiscard]] ChannelModel *channels();
    [[nodiscard]] FeedModel *feed();
    [[nodiscard]] LiveChannelModel *liveChannels();
    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] bool addingChannel() const;
    [[nodiscard]] bool apiKeyConfigured() const;
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QDateTime lastRefreshedAt() const;
    [[nodiscard]] qint64 selectedCategoryId() const;

    Q_INVOKABLE void startupRefresh();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectCategory(qint64 categoryId);
    Q_INVOKABLE bool addCategory(const QString &name);
    Q_INVOKABLE bool renameCategory(qint64 categoryId, const QString &name);
    Q_INVOKABLE bool removeCategory(qint64 categoryId);
    Q_INVOKABLE void addChannel(const QString &input, const QVariantList &categoryIds);
    Q_INVOKABLE bool removeChannel(const QString &channelId);
    Q_INVOKABLE bool setChannelInCategory(
        const QString &channelId,
        qint64 categoryId,
        bool member);
    Q_INVOKABLE bool setApiKey(const QString &apiKey, bool rememberLocally);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void openVideo(const QString &videoId);
    Q_INVOKABLE void clearError();

signals:
    void refreshingChanged();
    void addingChannelChanged();
    void apiKeyConfiguredChanged();
    void progressTextChanged();
    void statusMessageChanged();
    void errorMessageChanged();
    void lastRefreshedAtChanged();
    void selectedCategoryIdChanged();
    void channelAdded(QString title);

private:
    explicit AppController(QString databasePath, QObject *parent = nullptr);

    void reloadCategories();
    void reloadChannels();
    void reloadFeed();
    void setStatusMessage(QString message);
    void setErrorMessage(QString message);
    static QList<qint64> toCategoryIds(const QVariantList &values);

    Repository m_repository;
    YouTubeClient m_youTubeClient;
    RefreshService m_refreshService;
    CategoryModel m_categories;
    ChannelModel m_channels;
    FeedModel m_feed;
    LiveChannelModel m_liveChannels;
    bool m_initialized = false;
    bool m_startupRefreshRequested = false;
    bool m_addingChannel = false;
    QString m_statusMessage;
    QString m_errorMessage;
    QDateTime m_lastRefreshedAt;
    qint64 m_selectedCategoryId = -1;

    static AppController *s_instance;
};
