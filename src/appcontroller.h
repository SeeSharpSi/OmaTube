#pragma once

#include "models/categorymodel.h"
#include "models/channelmodel.h"
#include "models/feedmodel.h"
#include "models/historymodel.h"
#include "models/livechannelmodel.h"
#include "pointerwatch.h"
#include "refreshservice.h"
#include "repository.h"
#include "thememanager.h"
#include "watchtracker.h"
#include "youtubeclient.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
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
    Q_PROPERTY(HistoryModel *watchHistory READ watchHistory CONSTANT)
    Q_PROPERTY(LiveChannelModel *liveChannels READ liveChannels CONSTANT)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(bool historyHasMore READ historyHasMore NOTIFY historyHasMoreChanged)
    Q_PROPERTY(bool addingChannel READ addingChannel NOTIFY addingChannelChanged)
    Q_PROPERTY(bool apiKeyConfigured READ apiKeyConfigured NOTIFY apiKeyConfiguredChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressTextChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QDateTime lastRefreshedAt READ lastRefreshedAt NOTIFY lastRefreshedAtChanged)
    Q_PROPERTY(qint64 selectedCategoryId READ selectedCategoryId NOTIFY selectedCategoryIdChanged)
    Q_PROPERTY(int shortVideoCutoffMinutes READ shortVideoCutoffMinutes
                   NOTIFY shortVideoCutoffMinutesChanged)
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap themeColors READ themeColors NOTIFY themeChanged)
    Q_PROPERTY(bool omarchyThemeAvailable READ omarchyThemeAvailable NOTIFY themeChanged)
    Q_PROPERTY(QString omarchyThemeName READ omarchyThemeName NOTIFY themeChanged)
    Q_PROPERTY(QString currentVideoId READ currentVideoId NOTIFY currentVideoIdChanged)
    Q_PROPERTY(bool playerOpen READ playerOpen NOTIFY playerOpenChanged)
    Q_PROPERTY(int currentStartPosition READ currentStartPosition NOTIFY currentStartPositionChanged)
    Q_PROPERTY(PointerWatch *pointerWatcher READ pointerWatcher CONSTANT)
    Q_PROPERTY(QString videoBackend READ videoBackend NOTIFY videoBackendChanged)
    Q_PROPERTY(int maximumVideoHeight READ maximumVideoHeight NOTIFY maximumVideoHeightChanged)
    Q_PROPERTY(bool simpleUi READ simpleUi NOTIFY simpleUiChanged)
    Q_PROPERTY(bool mpvAvailable READ mpvAvailable CONSTANT)
    Q_PROPERTY(int playbackVolume READ playbackVolume NOTIFY playbackVolumeChanged)
    Q_PROPERTY(int currentVideoMaximumHeight READ currentVideoMaximumHeight NOTIFY
                   currentVideoMaximumHeightChanged)
    Q_PROPERTY(int currentVideoMaximumHeightOverride READ currentVideoMaximumHeightOverride NOTIFY
                   currentVideoMaximumHeightOverrideChanged)
    Q_PROPERTY(QString currentVideoTitle READ currentVideoTitle NOTIFY currentVideoTitleChanged)

public:
    ~AppController() override;

    static std::unique_ptr<AppController> createApplication(QString databasePath = {});
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool initialize(QString *error = nullptr);

    [[nodiscard]] CategoryModel *categories();
    [[nodiscard]] ChannelModel *channels();
    [[nodiscard]] FeedModel *feed();
    [[nodiscard]] HistoryModel *watchHistory();
    [[nodiscard]] LiveChannelModel *liveChannels();
    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] bool historyLoading() const;
    [[nodiscard]] bool historyHasMore() const;
    [[nodiscard]] bool addingChannel() const;
    [[nodiscard]] bool apiKeyConfigured() const;
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QDateTime lastRefreshedAt() const;
    [[nodiscard]] qint64 selectedCategoryId() const;
    [[nodiscard]] int shortVideoCutoffMinutes() const;
    [[nodiscard]] QString themeId() const;
    [[nodiscard]] QVariantMap themeColors() const;
    [[nodiscard]] bool omarchyThemeAvailable() const;
    [[nodiscard]] QString omarchyThemeName() const;
    [[nodiscard]] QString currentVideoId() const;
    [[nodiscard]] bool playerOpen() const;
    [[nodiscard]] int currentStartPosition() const;
    [[nodiscard]] PointerWatch *pointerWatcher();
    [[nodiscard]] QString videoBackend() const;
    [[nodiscard]] int maximumVideoHeight() const;
    [[nodiscard]] bool simpleUi() const;
    [[nodiscard]] bool mpvAvailable() const;
    [[nodiscard]] int playbackVolume() const;
    [[nodiscard]] int currentVideoMaximumHeight() const;
    [[nodiscard]] int currentVideoMaximumHeightOverride() const;
    [[nodiscard]] QString currentVideoTitle() const;

    Q_INVOKABLE void startupRefresh();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMoreHistory();
    Q_INVOKABLE void reloadWatchHistory();
    Q_INVOKABLE bool deleteWatchHistory(const QString &videoId);
    Q_INVOKABLE void selectCategory(qint64 categoryId);
    Q_INVOKABLE bool addCategory(const QString &name);
    Q_INVOKABLE bool renameCategory(qint64 categoryId, const QString &name);
    Q_INVOKABLE bool removeCategory(qint64 categoryId);
    Q_INVOKABLE bool moveCategory(qint64 categoryId, int targetIndex);
    Q_INVOKABLE void addChannel(const QString &input, const QVariantList &categoryIds);
    Q_INVOKABLE bool removeChannel(const QString &channelId);
    Q_INVOKABLE bool setChannelInCategory(
        const QString &channelId,
        qint64 categoryId,
        bool member);
    Q_INVOKABLE bool exportChannels(const QUrl &fileUrl);
    Q_INVOKABLE bool importChannels(const QUrl &fileUrl);
    Q_INVOKABLE bool exportCategories(const QUrl &fileUrl);
    Q_INVOKABLE bool importCategories(const QUrl &fileUrl);
    Q_INVOKABLE bool setApiKey(const QString &apiKey, bool rememberLocally);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void setShortVideoCutoffMinutes(int minutes);
    Q_INVOKABLE void setThemeId(const QString &themeId);
    Q_INVOKABLE void openVideo(const QString &videoId);
    Q_INVOKABLE void closePlayer();
    Q_INVOKABLE void setVideoBackend(const QString &backend);
    Q_INVOKABLE void setMaximumVideoHeight(int height);
    Q_INVOKABLE void setSimpleUi(bool enabled);
    Q_INVOKABLE void setPlaybackVolume(int volume);
    Q_INVOKABLE void setCurrentVideoMaximumHeightOverride(int height);
    Q_INVOKABLE void reportPlayback(const QString &videoId, double positionSeconds, bool playing);
    Q_INVOKABLE QVariantMap watchStatsForVideo(const QString &videoId);
    Q_INVOKABLE void clearError();

signals:
    void refreshingChanged();
    void historyLoadingChanged();
    void historyHasMoreChanged();
    void addingChannelChanged();
    void apiKeyConfiguredChanged();
    void progressTextChanged();
    void statusMessageChanged();
    void errorMessageChanged();
    void lastRefreshedAtChanged();
    void selectedCategoryIdChanged();
    void shortVideoCutoffMinutesChanged();
    void themeChanged();
    void currentVideoIdChanged();
    void playerOpenChanged();
    void currentStartPositionChanged();
    void videoBackendChanged();
    void maximumVideoHeightChanged();
    void simpleUiChanged();
    void playbackVolumeChanged();
    void currentVideoMaximumHeightChanged();
    void currentVideoMaximumHeightOverrideChanged();
    void currentVideoTitleChanged();
    void channelAdded(QString title);

private:
    explicit AppController(QString databasePath, QObject *parent = nullptr);

    void reloadCategories();
    void reloadChannels();
    void reloadFeed();
    void updateFeedCursor(const QList<Video> &videos);
    // Loads one further page of the feed starting at the current cursor and
    // appends it to the model.
    void appendFeedPage();
    void refreshHistoryHasMore();
    void setHistoryLoading(bool loading);
    void setHistoryHasMore(bool hasMore);
    [[nodiscard]] std::optional<qint64> feedCategoryScope() const;
    void resolveStartPosition(const QString &videoId);
    void flushWatchProgress();
    void setStatusMessage(QString message);
    void setErrorMessage(QString message);
    static QList<qint64> toCategoryIds(const QVariantList &values);
    static bool isValidVideoId(const QString &videoId);
    static QString perVideoHeightKey(const QString &videoId);
    void updateCurrentVideoMaximumHeightForOpen(const QString &videoId);
    void updateCurrentVideoTitleForOpen(const QString &videoId);

    static constexpr int feedPageSize = 50;

    ThemeManager m_themeManager;
    Repository m_repository;
    YouTubeClient m_youTubeClient;
    RefreshService m_refreshService;
    CategoryModel m_categories;
    ChannelModel m_channels;
    FeedModel m_feed;
    HistoryModel m_watchHistory;
    LiveChannelModel m_liveChannels;
    bool m_initialized = false;
    bool m_startupRefreshRequested = false;
    bool m_addingChannel = false;
    bool m_historyLoading = false;
    bool m_historyHasMore = false;
    QString m_statusMessage;
    QString m_errorMessage;
    QDateTime m_lastRefreshedAt;
    qint64 m_selectedCategoryId = -1;
    int m_shortVideoCutoffMinutes = 3;
    QDateTime m_feedCursorPublishedAt;
    QString m_feedCursorId;
    QString m_currentVideoId;
    bool m_playerOpen = false;
    int m_currentStartPosition = 0;
    QString m_videoBackend = QStringLiteral("iframe");
    int m_maximumVideoHeight = 0;
    bool m_simpleUi = false;
    int m_playbackVolume = 100;
    int m_currentVideoMaximumHeight = 0;
    int m_currentVideoMaximumHeightOverride = -1;
    QString m_currentVideoTitle;
    WatchTracker m_watchTracker;
    QTimer m_watchFlushTimer;

    static AppController *s_instance;
};
