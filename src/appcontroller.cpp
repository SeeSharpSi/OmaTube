#include "appcontroller.h"

#include "playbacksettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>

namespace {
constexpr auto apiKeySetting = "credentials/youtubeApiKey";
constexpr auto shortVideoCutoffSetting = "feed/shortVideoCutoffMinutes";
constexpr auto playbackBackendSetting = "playback/backend";
constexpr auto maximumVideoHeightSetting = "playback/maximumVideoHeight";
constexpr auto playbackVolumeSetting = "playback/volume";
constexpr auto simpleUiSetting = "appearance/simpleUi";
constexpr auto perVideoHeightPrefix = "playback/videoMaximumHeight/";
constexpr auto defaultPlaybackBackend = "iframe";
constexpr int defaultShortVideoCutoffMinutes = 3;
constexpr int maximumShortVideoCutoffMinutes = 60;
// Resume only when the stored position is past this many seconds.
constexpr int minimumResumePositionSeconds = 30;
// Positions within this distance of the end count as finished; do not resume.
constexpr int resumeEndThresholdSeconds = 90;
constexpr int watchFlushIntervalMs = 30'000;
constexpr auto channelsExportFormat = "omatube-channels";
constexpr auto categoriesExportFormat = "omatube-categories";
constexpr int exportFormatVersion = 1;

QString localFilePath(const QUrl &fileUrl, QString *error)
{
    if (!fileUrl.isLocalFile()) {
        if (error)
            *error = QStringLiteral("Choose a local file.");
        return {};
    }

    const QString path = fileUrl.toLocalFile();
    if (path.isEmpty() && error)
        *error = QStringLiteral("Choose a local file.");
    return path;
}

bool writeJsonFile(const QUrl &fileUrl, const QJsonObject &object, QString *error)
{
    const QString path = localFilePath(fileUrl, error);
    if (path.isEmpty())
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open export file: %1").arg(file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0) {
        if (error)
            *error = QStringLiteral("Could not write export file: %1").arg(file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save export file: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool readExportArray(
    const QUrl &fileUrl,
    const QString &expectedFormat,
    const QString &arrayName,
    QJsonArray *result,
    QString *error)
{
    const QString path = localFilePath(fileUrl, error);
    if (path.isEmpty())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open import file: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Import file is not a valid JSON object.");
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() != expectedFormat) {
        if (error)
            *error = QStringLiteral("Import file has the wrong OmaTube format.");
        return false;
    }
    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != exportFormatVersion) {
        if (error)
            *error = QStringLiteral("Import file version is not supported.");
        return false;
    }
    const QJsonValue entries = object.value(arrayName);
    if (!entries.isArray()) {
        if (error)
            *error = QStringLiteral("Import file is missing the '%1' array.").arg(arrayName);
        return false;
    }

    *result = entries.toArray();
    return true;
}

bool jsonString(
    const QJsonObject &object,
    const QString &name,
    bool required,
    QString *result,
    QString *error)
{
    const QJsonValue value = object.value(name);
    if (value.isUndefined() && !required) {
        result->clear();
        return true;
    }
    if (!value.isString() || (required && value.toString().trimmed().isEmpty())) {
        if (error)
            *error = QStringLiteral("Import field '%1' must be a non-empty string.").arg(name);
        return false;
    }
    *result = value.toString();
    return true;
}
}

AppController *AppController::s_instance = nullptr;

AppController::AppController(QString databasePath, QObject *parent, bool automationMode)
    : QObject(parent)
    , m_automationMode(automationMode)
    , m_themeManager(this)
    , m_repository(std::move(databasePath))
    , m_youTubeClient(this)
    , m_refreshService(&m_repository, &m_youTubeClient, this)
    , m_categories(this)
    , m_channels(this)
    , m_feed(this)
    , m_watchHistory(this)
    , m_watchNext(this)
    , m_liveChannels(this)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    m_watchFlushTimer.setInterval(watchFlushIntervalMs);
    connect(&m_watchFlushTimer, &QTimer::timeout, this, &AppController::flushWatchProgress);
    connect(
        qApp,
        &QCoreApplication::aboutToQuit,
        this,
        &AppController::flushWatchProgress);
    connect(&m_refreshService, &RefreshService::refreshingChanged,
            this, &AppController::refreshingChanged);
    connect(&m_refreshService, &RefreshService::progressTextChanged,
            this, &AppController::progressTextChanged);
    connect(&m_refreshService, &RefreshService::feedChanged, this, [this]() {
        reloadFeed();
        reloadWatchNext();
    });
    connect(
        &m_refreshService,
        &RefreshService::liveChannelsChanged,
        this,
        [this](QList<LiveChannel> live) { m_liveChannels.setLiveChannels(std::move(live)); });
    connect(
        &m_refreshService,
        &RefreshService::historyFinished,
        this,
        [this](const QString &error) {
            setHistoryLoading(false);
            if (!error.isEmpty())
                setErrorMessage(error);
            appendFeedPage();
        });
    connect(&m_themeManager, &ThemeManager::themeChanged, this, &AppController::themeChanged);
    connect(
        &m_refreshService,
        &RefreshService::refreshFinished,
        this,
        [this](
            QList<LiveChannel> live,
            bool liveComplete,
            const QString &feedError,
            const QString &liveError) {
            m_liveChannels.setLiveChannels(std::move(live));
            reloadFeed();
            reloadWatchNext();
            m_lastRefreshedAt = QDateTime::currentDateTime();
            emit lastRefreshedAtChanged();

            QStringList errors;
            if (!feedError.isEmpty())
                errors.append(QStringLiteral("Feed: %1").arg(feedError));
            if (!liveError.isEmpty())
                errors.append(QStringLiteral("Live status: %1").arg(liveError));
            setErrorMessage(errors.join(QStringLiteral("\n")));
            if (!feedError.isEmpty())
                setStatusMessage(QStringLiteral("Refresh completed with feed errors."));
            else if (!liveComplete)
                setStatusMessage(QStringLiteral("Feed updated. Live status is incomplete."));
            else
                setStatusMessage(QStringLiteral("Feed and live channels updated."));
        });
}

AppController::~AppController()
{
    flushWatchProgress();
    s_instance = nullptr;
}

std::unique_ptr<AppController> AppController::createApplication(
    QString databasePath,
    bool automationMode)
{
    return std::unique_ptr<AppController>(
        new AppController(std::move(databasePath), nullptr, automationMode));
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT(s_instance);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

bool AppController::initialize(QString *error)
{
    if (m_initialized)
        return true;
    if (!m_repository.open(error))
        return false;

    QSettings settings;
    if (m_automationMode) {
        m_youTubeClient.setApiKey({});
    } else {
        const QString environmentKey = qEnvironmentVariable("YT_CLIENT_API_KEY").trimmed();
        const QString storedKey =
            settings.value(QString::fromLatin1(apiKeySetting)).toString().trimmed();
        m_youTubeClient.setApiKey(environmentKey.isEmpty() ? storedKey : environmentKey);
    }
    m_shortVideoCutoffMinutes = qBound(
        0,
        settings.value(
                    QString::fromLatin1(shortVideoCutoffSetting),
                    defaultShortVideoCutoffMinutes)
            .toInt(),
        maximumShortVideoCutoffMinutes);
    QString storedBackend = PlaybackSettings::normalizeBackend(
        settings.value(QString::fromLatin1(playbackBackendSetting), QString::fromLatin1(defaultPlaybackBackend))
            .toString());
    if (!mpvAvailable() && storedBackend == QStringLiteral("mpv"))
        storedBackend = QStringLiteral("iframe");
    m_videoBackend = storedBackend;
    m_maximumVideoHeight = PlaybackSettings::normalizeMaximumVideoHeight(
        settings.value(QString::fromLatin1(maximumVideoHeightSetting)).toInt());
    m_playbackVolume = qBound(0, settings.value(QString::fromLatin1(playbackVolumeSetting), 100).toInt(), 100);
    m_simpleUi = settings.value(QString::fromLatin1(simpleUiSetting), false).toBool();
    m_currentVideoMaximumHeight = m_maximumVideoHeight;
    m_currentVideoMaximumHeightOverride = -1;
    m_currentVideoTitle.clear();
    reloadCategories();
    reloadChannels();
    reloadFeed();
    reloadWatchNext();
    m_initialized = true;
    return true;
}

CategoryModel *AppController::categories()
{
    return &m_categories;
}

ChannelModel *AppController::channels()
{
    return &m_channels;
}

FeedModel *AppController::feed()
{
    return &m_feed;
}

HistoryModel *AppController::watchHistory()
{
    return &m_watchHistory;
}

WatchNextModel *AppController::watchNext()
{
    return &m_watchNext;
}

LiveChannelModel *AppController::liveChannels()
{
    return &m_liveChannels;
}

bool AppController::refreshing() const
{
    return m_refreshService.refreshing();
}

bool AppController::historyLoading() const
{
    return m_historyLoading;
}

bool AppController::historyHasMore() const
{
    return m_historyHasMore;
}

bool AppController::addingChannel() const
{
    return m_addingChannel;
}

bool AppController::apiKeyConfigured() const
{
    return m_youTubeClient.hasApiKey();
}

QString AppController::progressText() const
{
    return m_refreshService.progressText();
}

QString AppController::statusMessage() const
{
    return m_statusMessage;
}

QString AppController::errorMessage() const
{
    return m_errorMessage;
}

QDateTime AppController::lastRefreshedAt() const
{
    return m_lastRefreshedAt;
}

qint64 AppController::selectedCategoryId() const
{
    return m_selectedCategoryId;
}

int AppController::shortVideoCutoffMinutes() const
{
    return m_shortVideoCutoffMinutes;
}

QString AppController::themeId() const
{
    return m_themeManager.selectedThemeId();
}

QVariantMap AppController::themeColors() const
{
    return m_themeManager.colors();
}

bool AppController::omarchyThemeAvailable() const
{
    return m_themeManager.omarchyThemeAvailable();
}

QString AppController::omarchyThemeName() const
{
    return m_themeManager.omarchyThemeName();
}

QString AppController::currentVideoId() const
{
    return m_currentVideoId;
}

bool AppController::playerOpen() const
{
    return m_playerOpen;
}

int AppController::currentStartPosition() const
{
    return m_currentStartPosition;
}

PointerWatch *AppController::pointerWatcher()
{
    static PointerWatch watcher;
    return &watcher;
}

QString AppController::videoBackend() const
{
    return m_videoBackend;
}

int AppController::maximumVideoHeight() const
{
    return m_maximumVideoHeight;
}

bool AppController::mpvAvailable() const
{
#if defined(OMA_HAS_MPV)
    return true;
#else
    return false;
#endif
}

int AppController::playbackVolume() const
{
    return m_playbackVolume;
}

bool AppController::simpleUi() const
{
    return m_simpleUi;
}

int AppController::currentVideoMaximumHeight() const
{
    return m_currentVideoMaximumHeight;
}

int AppController::currentVideoMaximumHeightOverride() const
{
    return m_currentVideoMaximumHeightOverride;
}

QString AppController::currentVideoTitle() const
{
    return m_currentVideoTitle;
}

bool AppController::automationMode() const
{
    return m_automationMode;
}

void AppController::startupRefresh()
{
    if (m_startupRefreshRequested)
        return;
    m_startupRefreshRequested = true;
    if (m_automationMode)
        return;
    refresh();
}

void AppController::refresh()
{
    if (!m_initialized || refreshing() || m_historyLoading)
        return;
    if (m_automationMode) {
        setStatusMessage(QStringLiteral("Automation mode: refresh is disabled."));
        return;
    }
    setErrorMessage({});
    setStatusMessage(QStringLiteral("Refreshing..."));
    m_refreshService.refresh();
}

void AppController::loadMoreHistory()
{
    if (!m_initialized || refreshing() || m_historyLoading || !m_historyHasMore)
        return;

    QString error;
    const QList<Video> videos = m_repository.feedPage(
        feedCategoryScope(),
        m_shortVideoCutoffMinutes * 60,
        m_feedCursorPublishedAt,
        m_feedCursorId,
        feedPageSize,
        &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    if (!videos.isEmpty()) {
        m_feed.appendVideos(videos);
        updateFeedCursor(videos);
        refreshHistoryHasMore();
        return;
    }

    if (m_automationMode)
        return;
    // Local cache exhausted; deepen remote history for the active scope.
    setHistoryLoading(true);
    m_refreshService.loadOlder(feedCategoryScope());
}

void AppController::reloadWatchHistory()
{
    QString error;
    const QList<HistoryEntry> entries = m_repository.watchHistory(&error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    m_watchHistory.setEntries(entries);
}

bool AppController::deleteWatchHistory(const QString &videoId)
{
    QString error;
    if (!m_repository.deleteWatchHistory(videoId, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadWatchHistory();
    setStatusMessage(QStringLiteral("Removed from watch history."));
    return true;
}

void AppController::reloadWatchNext()
{
    QString error;
    const QList<WatchNextEntry> entries = m_repository.watchNext(&error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    m_watchNext.setEntries(entries);
}

bool AppController::addToWatchNext(const QString &videoId)
{
    if (!isValidVideoId(videoId)) {
        setErrorMessage(QStringLiteral("Video URL is invalid."));
        return false;
    }
    QString error;
    if (m_repository.isInWatchNext(videoId, &error)) {
        if (!error.isEmpty())
            setErrorMessage(error);
        else
            setStatusMessage(QStringLiteral("Already in Watch Next."));
        return error.isEmpty();
    }
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return false;
    }
    if (!m_repository.addToWatchNext(videoId, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadWatchNext();
    setStatusMessage(QStringLiteral("Added to Watch Next."));
    return true;
}

bool AppController::removeFromWatchNext(const QString &videoId)
{
    QString error;
    if (!m_repository.removeFromWatchNext(videoId, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadWatchNext();
    setStatusMessage(QStringLiteral("Removed from Watch Next."));
    return true;
}

bool AppController::moveWatchNext(const QString &videoId, int targetIndex)
{
    QString error;
    if (!m_repository.moveWatchNext(videoId, targetIndex, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadWatchNext();
    return true;
}

bool AppController::isInWatchNext(const QString &videoId)
{
    QString error;
    const bool present = m_repository.isInWatchNext(videoId, &error);
    if (!error.isEmpty())
        setErrorMessage(error);
    return present;
}

void AppController::selectCategory(qint64 categoryId)
{
    const qint64 unselectedId = m_selectedCategoryId == categoryId ? -1 : categoryId;
    if (m_selectedCategoryId == unselectedId)
        return;
    m_selectedCategoryId = unselectedId;
    emit selectedCategoryIdChanged();
    reloadFeed();
}

bool AppController::addCategory(const QString &name)
{
    QString error;
    if (m_repository.addCategory(name, &error) < 0) {
        setErrorMessage(error);
        return false;
    }
    reloadCategories();
    reloadChannels();
    setStatusMessage(QStringLiteral("Category added."));
    return true;
}

bool AppController::renameCategory(qint64 categoryId, const QString &name)
{
    QString error;
    if (!m_repository.renameCategory(categoryId, name, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadCategories();
    setStatusMessage(QStringLiteral("Category renamed."));
    return true;
}

bool AppController::removeCategory(qint64 categoryId)
{
    QString error;
    if (!m_repository.removeCategory(categoryId, &error)) {
        setErrorMessage(error);
        return false;
    }
    if (m_selectedCategoryId == categoryId)
        selectCategory(-1);
    reloadCategories();
    reloadChannels();
    setStatusMessage(QStringLiteral("Category deleted. Channels were kept."));
    return true;
}

bool AppController::moveCategory(qint64 categoryId, int targetIndex)
{
    QString error;
    if (!m_repository.moveCategory(categoryId, targetIndex, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadCategories();
    setStatusMessage(QStringLiteral("Category moved."));
    return true;
}

void AppController::addChannel(const QString &input, const QVariantList &categoryIds)
{
    if (m_automationMode) {
        setErrorMessage(QStringLiteral("Automation mode: adding channels is disabled."));
        return;
    }
    if (m_addingChannel)
        return;
    m_addingChannel = true;
    emit addingChannelChanged();
    setErrorMessage({});
    const QList<qint64> ids = toCategoryIds(categoryIds);
    m_youTubeClient.resolveChannel(
        input,
        [this, ids](std::optional<Channel> channel, QString error) {
            m_addingChannel = false;
            emit addingChannelChanged();
            if (!error.isEmpty() || !channel) {
                setErrorMessage(error.isEmpty() ? QStringLiteral("Channel was not found.") : error);
                return;
            }

            QString databaseError;
            if (!m_repository.upsertChannel(*channel, &databaseError)
                || !m_repository.setChannelCategories(channel->id, ids, &databaseError)) {
                setErrorMessage(databaseError);
                return;
            }
            reloadChannels();
            setStatusMessage(
                QStringLiteral("%1 added. Press R to load its videos.").arg(channel->title));
            emit channelAdded(channel->title);
        });
}

bool AppController::removeChannel(const QString &channelId)
{
    QString error;
    if (!m_repository.removeChannel(channelId, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadChannels();
    reloadFeed();
    reloadWatchNext();
    setStatusMessage(QStringLiteral("Channel and its cached videos deleted."));
    return true;
}

bool AppController::setChannelInCategory(
    const QString &channelId,
    qint64 categoryId,
    bool member)
{
    QString error;
    if (!m_repository.setChannelCategoryMembership(channelId, categoryId, member, &error)) {
        setErrorMessage(error);
        return false;
    }
    reloadChannels();
    reloadFeed();
    return true;
}

bool AppController::exportChannels(const QUrl &fileUrl)
{
    QString error;
    const QList<Category> categories = m_repository.categories(&error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return false;
    }

    QHash<qint64, QString> categoryNames;
    for (const Category &category : categories)
        categoryNames.insert(category.id, category.name);

    const QList<Channel> channels = m_repository.channels(std::nullopt, &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return false;
    }

    QJsonArray channelArray;
    for (const Channel &channel : channels) {
        const QList<qint64> categoryIds = m_repository.categoryIdsForChannel(channel.id, &error);
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return false;
        }

        QJsonArray channelCategories;
        for (const qint64 categoryId : categoryIds) {
            if (categoryNames.contains(categoryId))
                channelCategories.append(categoryNames.value(categoryId));
        }
        channelArray.append(QJsonObject{
            {QStringLiteral("id"), channel.id},
            {QStringLiteral("originalInput"), channel.originalInput},
            {QStringLiteral("handle"), channel.handle},
            {QStringLiteral("title"), channel.title},
            {QStringLiteral("avatarUrl"), channel.avatarUrl},
            {QStringLiteral("uploadsPlaylistId"), channel.uploadsPlaylistId},
            {QStringLiteral("metadataFetchedAt"),
             channel.metadataFetchedAt.isValid()
                 ? channel.metadataFetchedAt.toUTC().toString(Qt::ISODateWithMs)
                 : QString()},
            {QStringLiteral("categories"), channelCategories},
        });
    }

    const QJsonObject object{
        {QStringLiteral("format"), QString::fromLatin1(channelsExportFormat)},
        {QStringLiteral("version"), exportFormatVersion},
        {QStringLiteral("channels"), channelArray},
    };
    if (!writeJsonFile(fileUrl, object, &error)) {
        setErrorMessage(error);
        return false;
    }

    setErrorMessage({});
    setStatusMessage(channelArray.size() == 1
                         ? QStringLiteral("Exported 1 channel.")
                         : QStringLiteral("Exported %1 channels.").arg(channelArray.size()));
    return true;
}

bool AppController::importChannels(const QUrl &fileUrl)
{
    QString error;
    QJsonArray channelArray;
    if (!readExportArray(
            fileUrl,
            QString::fromLatin1(channelsExportFormat),
            QStringLiteral("channels"),
            &channelArray,
            &error)) {
        setErrorMessage(error);
        return false;
    }

    QList<Repository::ChannelImportRecord> records;
    QSet<QString> channelIds;
    for (const QJsonValue &value : channelArray) {
        if (!value.isObject()) {
            setErrorMessage(QStringLiteral("Every channel entry must be a JSON object."));
            return false;
        }

        const QJsonObject object = value.toObject();
        Repository::ChannelImportRecord record;
        if (!jsonString(object, QStringLiteral("id"), true, &record.channel.id, &error)
            || !jsonString(
                object,
                QStringLiteral("originalInput"),
                false,
                &record.channel.originalInput,
                &error)
            || !jsonString(object, QStringLiteral("handle"), false, &record.channel.handle, &error)
            || !jsonString(object, QStringLiteral("title"), true, &record.channel.title, &error)
            || !jsonString(
                object,
                QStringLiteral("avatarUrl"),
                false,
                &record.channel.avatarUrl,
                &error)
            || !jsonString(
                object,
                QStringLiteral("uploadsPlaylistId"),
                true,
                &record.channel.uploadsPlaylistId,
                &error)) {
            setErrorMessage(error);
            return false;
        }
        record.channel.id = record.channel.id.trimmed();
        record.channel.title = record.channel.title.trimmed();
        record.channel.uploadsPlaylistId = record.channel.uploadsPlaylistId.trimmed();
        if (channelIds.contains(record.channel.id)) {
            setErrorMessage(QStringLiteral("Import file contains duplicate channel IDs."));
            return false;
        }
        channelIds.insert(record.channel.id);

        const QJsonValue fetchedAt = object.value(QStringLiteral("metadataFetchedAt"));
        if (!fetchedAt.isUndefined()) {
            if (!fetchedAt.isString()) {
                setErrorMessage(QStringLiteral("Import field 'metadataFetchedAt' must be a string."));
                return false;
            }
            const QString timestamp = fetchedAt.toString();
            if (!timestamp.isEmpty()) {
                record.channel.metadataFetchedAt = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
                if (!record.channel.metadataFetchedAt.isValid()) {
                    setErrorMessage(QStringLiteral("Import field 'metadataFetchedAt' is invalid."));
                    return false;
                }
                record.channel.metadataFetchedAt = record.channel.metadataFetchedAt.toUTC();
            }
        }

        const QJsonValue categories = object.value(QStringLiteral("categories"));
        if (!categories.isUndefined()) {
            if (!categories.isArray()) {
                setErrorMessage(QStringLiteral("Import field 'categories' must be an array."));
                return false;
            }
            QSet<QString> names;
            for (const QJsonValue &categoryValue : categories.toArray()) {
                if (!categoryValue.isString() || categoryValue.toString().trimmed().isEmpty()) {
                    setErrorMessage(QStringLiteral("Channel category names must be non-empty strings."));
                    return false;
                }
                const QString name = categoryValue.toString().trimmed();
                if (!names.contains(name)) {
                    names.insert(name);
                    record.categoryNames.append(name);
                }
            }
        }
        records.append(std::move(record));
    }

    if (!m_repository.importChannels(records, &error)) {
        setErrorMessage(error);
        return false;
    }
    setErrorMessage({});
    reloadCategories();
    reloadChannels();
    reloadFeed();
    setStatusMessage(records.size() == 1
                         ? QStringLiteral("Imported 1 channel.")
                         : QStringLiteral("Imported %1 channels.").arg(records.size()));
    return true;
}

bool AppController::exportCategories(const QUrl &fileUrl)
{
    QString error;
    const QList<Category> categories = m_repository.categories(&error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return false;
    }

    QJsonArray categoryArray;
    for (const Category &category : categories) {
        const QList<Channel> channels = m_repository.channels(category.id, &error);
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return false;
        }
        QJsonArray channelIds;
        for (const Channel &channel : channels)
            channelIds.append(channel.id);
        categoryArray.append(QJsonObject{
            {QStringLiteral("name"), category.name},
            {QStringLiteral("channelIds"), channelIds},
        });
    }

    const QJsonObject object{
        {QStringLiteral("format"), QString::fromLatin1(categoriesExportFormat)},
        {QStringLiteral("version"), exportFormatVersion},
        {QStringLiteral("categories"), categoryArray},
    };
    if (!writeJsonFile(fileUrl, object, &error)) {
        setErrorMessage(error);
        return false;
    }

    setErrorMessage({});
    setStatusMessage(categoryArray.size() == 1
                         ? QStringLiteral("Exported 1 category.")
                         : QStringLiteral("Exported %1 categories.").arg(categoryArray.size()));
    return true;
}

bool AppController::importCategories(const QUrl &fileUrl)
{
    QString error;
    QJsonArray categoryArray;
    if (!readExportArray(
            fileUrl,
            QString::fromLatin1(categoriesExportFormat),
            QStringLiteral("categories"),
            &categoryArray,
            &error)) {
        setErrorMessage(error);
        return false;
    }

    QList<Repository::CategoryImportRecord> records;
    QSet<QString> categoryNames;
    for (const QJsonValue &value : categoryArray) {
        if (!value.isObject()) {
            setErrorMessage(QStringLiteral("Every category entry must be a JSON object."));
            return false;
        }

        const QJsonObject object = value.toObject();
        Repository::CategoryImportRecord record;
        if (!jsonString(object, QStringLiteral("name"), true, &record.name, &error)) {
            setErrorMessage(error);
            return false;
        }
        record.name = record.name.trimmed();
        if (categoryNames.contains(record.name)) {
            setErrorMessage(QStringLiteral("Import file contains duplicate category names."));
            return false;
        }
        categoryNames.insert(record.name);

        const QJsonValue channelIds = object.value(QStringLiteral("channelIds"));
        if (!channelIds.isUndefined()) {
            if (!channelIds.isArray()) {
                setErrorMessage(QStringLiteral("Import field 'channelIds' must be an array."));
                return false;
            }
            QSet<QString> ids;
            for (const QJsonValue &channelValue : channelIds.toArray()) {
                if (!channelValue.isString() || channelValue.toString().trimmed().isEmpty()) {
                    setErrorMessage(QStringLiteral("Category channel IDs must be non-empty strings."));
                    return false;
                }
                const QString channelId = channelValue.toString().trimmed();
                if (!ids.contains(channelId)) {
                    ids.insert(channelId);
                    record.channelIds.append(channelId);
                }
            }
        }
        records.append(std::move(record));
    }

    if (!m_repository.importCategories(records, &error)) {
        setErrorMessage(error);
        return false;
    }
    setErrorMessage({});
    reloadCategories();
    reloadChannels();
    reloadFeed();
    setStatusMessage(records.size() == 1
                         ? QStringLiteral("Imported 1 category.")
                         : QStringLiteral("Imported %1 categories.").arg(records.size()));
    return true;
}

bool AppController::setApiKey(const QString &apiKey, bool rememberLocally)
{
    const QString trimmedKey = apiKey.trimmed();
    if (trimmedKey.isEmpty()) {
        setErrorMessage(QStringLiteral("API key cannot be empty."));
        return false;
    }

    m_youTubeClient.setApiKey(trimmedKey);
    QSettings settings;
    if (rememberLocally)
        settings.setValue(QString::fromLatin1(apiKeySetting), trimmedKey);
    else
        settings.remove(QString::fromLatin1(apiKeySetting));
    settings.sync();
    emit apiKeyConfiguredChanged();
    setErrorMessage({});
    setStatusMessage(QStringLiteral("API key configured. Press R to refresh."));
    return true;
}

void AppController::clearApiKey()
{
    m_youTubeClient.setApiKey({});
    QSettings settings;
    settings.remove(QString::fromLatin1(apiKeySetting));
    settings.sync();
    emit apiKeyConfiguredChanged();
    setStatusMessage(QStringLiteral("Stored API key removed."));
}

void AppController::setShortVideoCutoffMinutes(int minutes)
{
    const int boundedMinutes = qBound(0, minutes, maximumShortVideoCutoffMinutes);
    if (m_shortVideoCutoffMinutes == boundedMinutes)
        return;

    m_shortVideoCutoffMinutes = boundedMinutes;
    QSettings settings;
    settings.setValue(QString::fromLatin1(shortVideoCutoffSetting), boundedMinutes);
    settings.sync();
    emit shortVideoCutoffMinutesChanged();
    if (m_initialized)
        reloadFeed();
}

void AppController::setThemeId(const QString &themeId)
{
    m_themeManager.setSelectedThemeId(themeId);
}

void AppController::openVideo(const QString &videoId)
{
    static const QRegularExpression videoIdExpression(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    if (!videoIdExpression.match(videoId).hasMatch()) {
        setErrorMessage(QStringLiteral("Video URL is invalid."));
        return;
    }
    m_watchTracker.setActiveVideo(videoId, watchStatsForVideo(videoId)
                                               .value(QStringLiteral("lastPositionSeconds"))
                                               .toInt());
    flushWatchProgress();
    // Resolve playback inputs before notifying QML that a new video is active.
    // The player can already exist when switching videos, so it must not load
    // with the previous video's resume position.
    resolveStartPosition(videoId);
    if (m_currentVideoId != videoId) {
        m_currentVideoId = videoId;
        emit currentVideoIdChanged();
    }
    updateCurrentVideoTitleForOpen(videoId);
    updateCurrentVideoMaximumHeightForOpen(videoId);
    m_watchFlushTimer.start();
    if (!m_playerOpen) {
        m_playerOpen = true;
        emit playerOpenChanged();
    }
}

void AppController::closePlayer()
{
    if (!m_playerOpen)
        return;
    m_watchTracker.clearActiveVideo();
    flushWatchProgress();
    m_watchFlushTimer.stop();
    reloadFeed();
    reloadWatchNext();
    m_playerOpen = false;
    emit playerOpenChanged();
}

void AppController::setVideoBackend(const QString &backend)
{
    const QString normalized = PlaybackSettings::normalizeBackend(backend);
    if (normalized == QStringLiteral("mpv") && !mpvAvailable()) {
        setErrorMessage(QStringLiteral("Embedded mpv is unavailable in this build."));
        return;
    }
    if (m_videoBackend == normalized)
        return;

    m_videoBackend = normalized;
    QSettings settings;
    settings.setValue(QString::fromLatin1(playbackBackendSetting), normalized);
    settings.sync();
    emit videoBackendChanged();
}

void AppController::setMaximumVideoHeight(int height)
{
    const int normalized = PlaybackSettings::normalizeMaximumVideoHeight(height);
    if (m_maximumVideoHeight == normalized)
        return;

    m_maximumVideoHeight = normalized;
    QSettings settings;
    settings.setValue(QString::fromLatin1(maximumVideoHeightSetting), normalized);
    settings.sync();
    emit maximumVideoHeightChanged();
    if (m_currentVideoMaximumHeightOverride == -1) {
        if (m_currentVideoMaximumHeight != normalized) {
            m_currentVideoMaximumHeight = normalized;
            emit currentVideoMaximumHeightChanged();
        }
    }
}

void AppController::setPlaybackVolume(int volume)
{
    const int clamped = qBound(0, volume, 100);
    if (m_playbackVolume == clamped)
        return;
    m_playbackVolume = clamped;
    QSettings settings;
    settings.setValue(QString::fromLatin1(playbackVolumeSetting), clamped);
    settings.sync();
    emit playbackVolumeChanged();
}

void AppController::setSimpleUi(bool enabled)
{
    if (m_simpleUi == enabled)
        return;

    m_simpleUi = enabled;
    QSettings settings;
    settings.setValue(QString::fromLatin1(simpleUiSetting), enabled);
    settings.sync();
    emit simpleUiChanged();
}

void AppController::setCurrentVideoMaximumHeightOverride(int height)
{
    if (!isValidVideoId(m_currentVideoId))
        return;
    int overrideValue = -1;
    int effective = m_maximumVideoHeight;
    QSettings settings;
    const QString key = perVideoHeightKey(m_currentVideoId);
    if (height == -1) {
        settings.remove(key);
        settings.sync();
        overrideValue = -1;
        effective = m_maximumVideoHeight;
    } else {
        const int normalized = PlaybackSettings::normalizeMaximumVideoHeight(height);
        settings.setValue(key, normalized);
        settings.sync();
        overrideValue = normalized;
        effective = normalized;
    }
    bool overrideChanged = m_currentVideoMaximumHeightOverride != overrideValue;
    bool effectiveChanged = m_currentVideoMaximumHeight != effective;
    m_currentVideoMaximumHeightOverride = overrideValue;
    m_currentVideoMaximumHeight = effective;
    if (overrideChanged)
        emit currentVideoMaximumHeightOverrideChanged();
    if (effectiveChanged)
        emit currentVideoMaximumHeightChanged();
}

void AppController::reportPlayback(const QString &videoId, double positionSeconds, bool playing)
{
    if (!m_playerOpen)
        return;
    m_watchTracker.reportPlayback(videoId, positionSeconds, playing);
}

QVariantMap AppController::watchStatsForVideo(const QString &videoId)
{
    QString error;
    const std::optional<WatchStats> stats = m_repository.watchStats(videoId, &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return {};
    }
    if (!stats)
        return {};
    return {
        {QStringLiteral("videoId"), stats->videoId},
        {QStringLiteral("watchedSeconds"), stats->watchedSeconds},
        {QStringLiteral("lastPositionSeconds"), stats->lastPositionSeconds},
        {QStringLiteral("lastWatchedAt"), stats->lastWatchedAt},
        {QStringLiteral("watchCount"), stats->watchCount},
    };
}

void AppController::clearError()
{
    setErrorMessage({});
}

void AppController::reloadCategories()
{
    QString error;
    m_categories.setCategories(m_repository.categories(&error));
    if (!error.isEmpty())
        setErrorMessage(error);
}

void AppController::reloadChannels()
{
    QString error;
    const QList<Channel> allChannels = m_repository.channels(std::nullopt, &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    QHash<QString, QList<qint64>> categoryIds;
    for (const Channel &channel : allChannels) {
        categoryIds.insert(
            channel.id,
            m_repository.categoryIdsForChannel(channel.id, &error));
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return;
        }
    }
    m_channels.setChannels(allChannels, categoryIds);
}

std::optional<qint64> AppController::feedCategoryScope() const
{
    return m_selectedCategoryId < 0
        ? std::optional<qint64>(std::nullopt)
        : std::optional<qint64>(m_selectedCategoryId);
}

void AppController::reloadFeed()
{
    QString error;
    const QList<Video> videos = m_repository.feedPage(
        feedCategoryScope(),
        m_shortVideoCutoffMinutes * 60,
        {},
        {},
        feedPageSize,
        &error);
    m_feed.setVideos(videos);
    updateFeedCursor(videos);
    if (!error.isEmpty())
        setErrorMessage(error);
    refreshHistoryHasMore();
}

void AppController::updateFeedCursor(const QList<Video> &videos)
{
    if (videos.isEmpty()) {
        m_feedCursorPublishedAt = {};
        m_feedCursorId.clear();
        return;
    }
    const Video &last = videos.last();
    m_feedCursorPublishedAt = last.publishedAt;
    m_feedCursorId = last.id;
}

void AppController::appendFeedPage()
{
    QString error;
    const QList<Video> videos = m_repository.feedPage(
        feedCategoryScope(),
        m_shortVideoCutoffMinutes * 60,
        m_feedCursorPublishedAt,
        m_feedCursorId,
        feedPageSize,
        &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    if (!videos.isEmpty()) {
        m_feed.appendVideos(videos);
        updateFeedCursor(videos);
    }
    refreshHistoryHasMore();
}

void AppController::refreshHistoryHasMore()
{
    QString error;
    bool hasMore = false;
    if (!m_repository.feedPage(
            feedCategoryScope(),
            m_shortVideoCutoffMinutes * 60,
            m_feedCursorPublishedAt,
            m_feedCursorId,
            1,
            &error)
             .isEmpty()) {
        hasMore = true;
    } else if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    } else if (!m_automationMode && m_startupRefreshRequested && m_youTubeClient.canFetchHistory()
               && m_repository.canFetchMoreHistory()) {
        hasMore = m_repository.historyIncomplete(feedCategoryScope(), &error);
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return;
        }
    }
    setHistoryHasMore(hasMore);
}

void AppController::setHistoryLoading(bool loading)
{
    if (m_historyLoading == loading)
        return;
    m_historyLoading = loading;
    emit historyLoadingChanged();
}

void AppController::setHistoryHasMore(bool hasMore)
{
    if (m_historyHasMore == hasMore)
        return;
    m_historyHasMore = hasMore;
    emit historyHasMoreChanged();
}

void AppController::resolveStartPosition(const QString &videoId)
{
    int startPosition = 0;
    QString error;
    const std::optional<WatchStats> stats = m_repository.watchStats(videoId, &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        return;
    }
    if (stats && stats->lastPositionSeconds >= minimumResumePositionSeconds) {
        const std::optional<Video> video = m_repository.video(videoId, &error);
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return;
        }
        // Unknown metadata resumes normally; broadcasts and near-finished
        // videos restart from the beginning.
        const bool finished = video && video->durationSeconds > 0
            && stats->lastPositionSeconds >= video->durationSeconds - resumeEndThresholdSeconds;
        if (!video || (!video->isBroadcast && !finished))
            startPosition = stats->lastPositionSeconds;
    }

    if (m_currentStartPosition == startPosition)
        return;
    m_currentStartPosition = startPosition;
    emit currentStartPositionChanged();
}

void AppController::flushWatchProgress()
{
    const QList<WatchProgressUpdate> updates = m_watchTracker.takePendingUpdates();
    for (const WatchProgressUpdate &update : updates) {
        QString error;
        if (!m_repository.applyWatchProgress(
                update.videoId,
                update.watchedSecondsDelta,
                update.lastPositionSeconds,
                update.countSession,
                &error)) {
            setErrorMessage(error);
        }
    }
}

void AppController::setStatusMessage(QString message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = std::move(message);
    emit statusMessageChanged();
}

void AppController::setErrorMessage(QString message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = std::move(message);
    emit errorMessageChanged();
}

QList<qint64> AppController::toCategoryIds(const QVariantList &values)
{
    QList<qint64> result;
    result.reserve(values.size());
    for (const QVariant &value : values) {
        bool valid = false;
        const qint64 id = value.toLongLong(&valid);
        if (valid && id >= 0 && !result.contains(id))
            result.append(id);
    }
    return result;
}

bool AppController::isValidVideoId(const QString &videoId)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return re.match(videoId).hasMatch();
}

QString AppController::perVideoHeightKey(const QString &videoId)
{
    return QString::fromLatin1(perVideoHeightPrefix) + videoId;
}

void AppController::updateCurrentVideoMaximumHeightForOpen(const QString &videoId)
{
    QSettings settings;
    const QString key = perVideoHeightKey(videoId);
    int overrideValue = -1;
    int effective = m_maximumVideoHeight;
    if (settings.contains(key)) {
        const int stored = PlaybackSettings::normalizeMaximumVideoHeight(settings.value(key).toInt());
        overrideValue = stored;
        effective = stored;
    }
    bool overrideChanged = m_currentVideoMaximumHeightOverride != overrideValue;
    bool effectiveChanged = m_currentVideoMaximumHeight != effective;
    m_currentVideoMaximumHeightOverride = overrideValue;
    m_currentVideoMaximumHeight = effective;
    if (overrideChanged)
        emit currentVideoMaximumHeightOverrideChanged();
    if (effectiveChanged)
        emit currentVideoMaximumHeightChanged();
}

void AppController::updateCurrentVideoTitleForOpen(const QString &videoId)
{
    QString title;
    QString error;
    const std::optional<Video> video = m_repository.video(videoId, &error);
    if (!error.isEmpty()) {
        setErrorMessage(error);
        title.clear();
    } else if (video) {
        title = video->title;
    } else {
        title.clear();
    }
    if (m_currentVideoTitle == title)
        return;
    m_currentVideoTitle = title;
    emit currentVideoTitleChanged();
}
