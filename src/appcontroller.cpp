#include "appcontroller.h"

#include <QDesktopServices>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace {
constexpr auto apiKeySetting = "credentials/youtubeApiKey";
}

AppController *AppController::s_instance = nullptr;

AppController::AppController(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_repository(std::move(databasePath))
    , m_youTubeClient(this)
    , m_refreshService(&m_repository, &m_youTubeClient, this)
    , m_categories(this)
    , m_channels(this)
    , m_feed(this)
    , m_liveChannels(this)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    connect(&m_refreshService, &RefreshService::refreshingChanged,
            this, &AppController::refreshingChanged);
    connect(&m_refreshService, &RefreshService::progressTextChanged,
            this, &AppController::progressTextChanged);
    connect(&m_refreshService, &RefreshService::feedChanged, this, &AppController::reloadFeed);
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
    s_instance = nullptr;
}

std::unique_ptr<AppController> AppController::createApplication(QString databasePath)
{
    return std::unique_ptr<AppController>(new AppController(std::move(databasePath)));
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

    const QString environmentKey = qEnvironmentVariable("YT_CLIENT_API_KEY").trimmed();
    const QString storedKey = QSettings().value(QString::fromLatin1(apiKeySetting)).toString().trimmed();
    m_youTubeClient.setApiKey(environmentKey.isEmpty() ? storedKey : environmentKey);
    reloadCategories();
    reloadChannels();
    reloadFeed();
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

LiveChannelModel *AppController::liveChannels()
{
    return &m_liveChannels;
}

bool AppController::refreshing() const
{
    return m_refreshService.refreshing();
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

void AppController::startupRefresh()
{
    if (m_startupRefreshRequested)
        return;
    m_startupRefreshRequested = true;
    if (!apiKeyConfigured()) {
        setStatusMessage(QStringLiteral("Add a YouTube API key, then press R to refresh."));
        return;
    }
    refresh();
}

void AppController::refresh()
{
    if (!m_initialized || refreshing())
        return;
    setErrorMessage({});
    setStatusMessage(QStringLiteral("Refreshing..."));
    m_refreshService.refresh();
}

void AppController::selectCategory(qint64 categoryId)
{
    if (m_selectedCategoryId == categoryId)
        return;
    m_selectedCategoryId = categoryId;
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

void AppController::addChannel(const QString &input, const QVariantList &categoryIds)
{
    if (m_addingChannel)
        return;
    if (!apiKeyConfigured()) {
        setErrorMessage(QStringLiteral("Add a YouTube API key before adding channels."));
        return;
    }

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

void AppController::openVideo(const QString &videoId)
{
    static const QRegularExpression videoIdExpression(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    if (!videoIdExpression.match(videoId).hasMatch()) {
        setErrorMessage(QStringLiteral("Video URL is invalid."));
        return;
    }
    if (!QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId)))) {
        setErrorMessage(QStringLiteral("Could not open the system browser."));
    }
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

void AppController::reloadFeed()
{
    QString error;
    const std::optional<qint64> categoryId = m_selectedCategoryId < 0
        ? std::nullopt
        : std::optional<qint64>(m_selectedCategoryId);
    m_feed.setVideos(m_repository.feed(categoryId, 500, &error));
    if (!error.isEmpty())
        setErrorMessage(error);
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
