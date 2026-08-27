#include "mpvplayer.h"
#include "playbacksettings.h"

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QtQuick/qquickframebufferobject.h>
#include <QtQuick/qquickwindow.h>
#include <QtQuick/qquickopenglutils.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

// Shared between the item (GUI thread) and the renderer (Qt render thread).
// The mpv core handle is created/destroyed on the GUI thread, while the
// mpv_render_context is created/destroyed on the render thread with the GL
// context current. Both callbacks (wakeup + render update) never touch libmpv
// directly; they only marshal work back to the GUI thread via queued calls.
class MpvCore final : public QObject
{
    Q_OBJECT

public:
    MpvCore() = default;
    ~MpvCore() override;

    bool initialize();
    bool initialized() const { return m_handle != nullptr; }
    QString initError() const { return m_initError; }

    mpv_handle *handle() const { return m_handle; }
    mpv_render_context *renderContext() const { return m_renderContext; }
    bool hasRenderContext() const { return m_renderContext != nullptr; }

    // Accessed on the render thread with the GL context current.
    bool createRenderContextIfNeeded();
    void freeRenderContext();

    // Cached property snapshots (kept up to date by observe-property events).
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool buffering() const { return m_buffering; }
    bool idleActive() const { return m_idleActive; }

    // Commands, all safe to call from the GUI thread.
    void loadUrl(const QUrl &url);
    void seek(double seconds);
    void setPaused(bool paused);
    void setVolume(int volume);
    void setMuted(bool muted);
    void stop();
    void setYtdlFormat(const QString &format);
    void clearYtdlFormat();
    void setStartTime(int seconds);

    // Teardown (GUI thread): stop event dispatch, drop the wakeup callback so
    // no further libmpv API calls are made after the item is gone.
    void shutdown();

signals:
    void eventsProcessed();
    void fileLoaded();
    void playbackEndFile(int reason, int error);
    void errorMessage(const QString &message);
    void renderContextReady();
    void renderContextFailed(const QString &message);
    void frameNeedsUpdate();

private slots:
    void pumpEvents();
    void requestFrameUpdate();

private:
    static void wakeupCb(void *ctx);
    static void renderUpdateCb(void *ctx);
    static void *getProcAddress(void *ctx, const char *name);
    void handlePropertyChange(const mpv_event_property *prop);

    mpv_handle *m_handle = nullptr;
    mpv_render_context *m_renderContext = nullptr;

    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = false;
    int m_volume = 100;
    bool m_muted = false;
    bool m_buffering = false;
    bool m_idleActive = false;

    QString m_initError;
    std::atomic_bool m_shutdown{false};
    std::atomic_bool m_renderContextFailed{false};
};

void MpvCore::wakeupCb(void *ctx)
{
    auto *self = static_cast<MpvCore *>(ctx);
    QMetaObject::invokeMethod(self, &MpvCore::pumpEvents, Qt::QueuedConnection);
}

void MpvCore::renderUpdateCb(void *ctx)
{
    auto *self = static_cast<MpvCore *>(ctx);
    QMetaObject::invokeMethod(self, &MpvCore::requestFrameUpdate, Qt::QueuedConnection);
}

void MpvCore::requestFrameUpdate()
{
    emit frameNeedsUpdate();
}

void *MpvCore::getProcAddress(void *, const char *name)
{
    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    if (!glContext)
        return nullptr;
    return reinterpret_cast<void *>(glContext->getProcAddress(name));
}

MpvCore::~MpvCore()
{
    // The renderer frees m_renderContext (render thread, GL current) before the
    // core is released, so by construction it is already null here. Free it as
    // a defensive fallback so mpv_destroy never runs with a live render context.
    if (m_renderContext) {
        mpv_render_context_free(m_renderContext);
        m_renderContext = nullptr;
    }
    if (m_handle) {
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);
        mpv_destroy(m_handle);
        m_handle = nullptr;
    }
}

bool MpvCore::initialize()
{
    m_handle = mpv_create();
    if (!m_handle) {
        m_initError = QStringLiteral("Could not create the mpv player.");
        return false;
    }

    // Options must be set before mpv_initialize().
    const char *options[][2] = {
        { "config", "no" },
        { "terminal", "no" },
        { "vo", "libmpv" },
        { "idle", "yes" },
        { "keep-open", "yes" },
        { "hwdec", "auto-safe" },
        { "ytdl", "yes" },
    };
    for (const auto &option : options)
        mpv_set_option_string(m_handle, option[0], option[1]);

    if (mpv_initialize(m_handle) < 0) {
        mpv_destroy(m_handle);
        m_handle = nullptr;
        m_initError = QStringLiteral("Could not initialize the mpv player.");
        return false;
    }

    mpv_set_wakeup_callback(m_handle, &MpvCore::wakeupCb, this);

    mpv_observe_property(m_handle, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_handle, 0, "volume", MPV_FORMAT_INT64);
    mpv_observe_property(m_handle, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_handle, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_handle, 0, "idle-active", MPV_FORMAT_FLAG);

    return true;
}

void MpvCore::shutdown()
{
    m_shutdown = true;
    if (m_handle)
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);
}

void MpvCore::handlePropertyChange(const mpv_event_property *prop)
{
    if (!prop || prop->format == MPV_FORMAT_NONE || !prop->data)
        return;
    const char *name = prop->name ? prop->name : "";

    if (prop->format == MPV_FORMAT_DOUBLE && std::strcmp(name, "time-pos") == 0) {
        m_position = *static_cast<double *>(prop->data);
    } else if (prop->format == MPV_FORMAT_DOUBLE && std::strcmp(name, "duration") == 0) {
        m_duration = *static_cast<double *>(prop->data);
    } else if (prop->format == MPV_FORMAT_FLAG && std::strcmp(name, "pause") == 0) {
        m_paused = *static_cast<int *>(prop->data) != 0;
    } else if (prop->format == MPV_FORMAT_INT64 && std::strcmp(name, "volume") == 0) {
        m_volume = static_cast<int>(*static_cast<int64_t *>(prop->data));
    } else if (prop->format == MPV_FORMAT_FLAG && std::strcmp(name, "mute") == 0) {
        m_muted = *static_cast<int *>(prop->data) != 0;
    } else if (prop->format == MPV_FORMAT_FLAG && std::strcmp(name, "paused-for-cache") == 0) {
        m_buffering = *static_cast<int *>(prop->data) != 0;
    } else if (prop->format == MPV_FORMAT_FLAG && std::strcmp(name, "idle-active") == 0) {
        m_idleActive = *static_cast<int *>(prop->data) != 0;
    }
}

void MpvCore::pumpEvents()
{
    // The item may already be gone; never touch libmpv once shutdown is set.
    if (!m_handle || m_shutdown)
        return;

    mpv_event *ev = nullptr;
    while ((ev = mpv_wait_event(m_handle, 0))->event_id != MPV_EVENT_NONE) {
        switch (ev->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            handlePropertyChange(static_cast<mpv_event_property *>(ev->data));
            break;
        case MPV_EVENT_FILE_LOADED:
            emit fileLoaded();
            break;
        case MPV_EVENT_END_FILE:
            if (ev->data) {
                auto *endFile = static_cast<mpv_event_end_file *>(ev->data);
                emit playbackEndFile(endFile->reason, endFile->error);
            }
            break;
        case MPV_EVENT_COMMAND_REPLY:
            if (ev->error < 0)
                emit errorMessage(QString::fromUtf8(mpv_error_string(ev->error)));
            break;
        default:
            break;
        }
    }
    emit eventsProcessed();
}

void MpvCore::loadUrl(const QUrl &url)
{
    if (!m_handle)
        return;
    const QByteArray target = url.toString().toUtf8();
    const char *command[] = { "loadfile", target.constData(), nullptr };
    const int result = mpv_command(m_handle, command);
    if (result < 0)
        emit errorMessage(QString::fromUtf8(mpv_error_string(result)));
}

void MpvCore::seek(double seconds)
{
    if (!m_handle)
        return;
    const QByteArray position = QByteArray::number(seconds, 'f', 3);
    const char *command[] = { "seek", position.constData(), "absolute", nullptr };
    mpv_command(m_handle, command);
}

void MpvCore::setPaused(bool paused)
{
    if (!m_handle)
        return;
    mpv_set_property_string(m_handle, "pause", paused ? "yes" : "no");
}

void MpvCore::setVolume(int volume)
{
    if (!m_handle)
        return;
    const QByteArray value = QByteArray::number(volume);
    mpv_set_property_string(m_handle, "volume", value.constData());
}

void MpvCore::setMuted(bool muted)
{
    if (!m_handle)
        return;
    mpv_set_property_string(m_handle, "mute", muted ? "yes" : "no");
}

void MpvCore::stop()
{
    if (!m_handle)
        return;
    const char *command[] = { "stop", nullptr };
    mpv_command(m_handle, command);
}

void MpvCore::setYtdlFormat(const QString &format)
{
    if (!m_handle)
        return;
    mpv_set_property_string(m_handle, "ytdl-format", format.toUtf8().constData());
}

void MpvCore::clearYtdlFormat()
{
    if (!m_handle)
        return;
    mpv_del_property(m_handle, "ytdl-format");
}

void MpvCore::setStartTime(int seconds)
{
    if (!m_handle)
        return;
    if (seconds > 0)
        mpv_set_property_string(m_handle, "start", QByteArray::number(seconds).constData());
    else
        mpv_del_property(m_handle, "start");
}

bool MpvCore::createRenderContextIfNeeded()
{
    if (m_renderContext)
        return true;
    if (m_renderContextFailed || m_shutdown)
        return false;

    mpv_opengl_init_params glInit{ &MpvCore::getProcAddress, nullptr };
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };

    mpv_render_context *context = nullptr;
    if (!m_handle || mpv_render_context_create(&context, m_handle, params) < 0) {
        m_renderContextFailed = true;
        QMetaObject::invokeMethod(this, [this] {
            emit renderContextFailed(QStringLiteral("OpenGL video initialization failed."));
        }, Qt::QueuedConnection);
        return false;
    }

    m_renderContext = context;
    mpv_render_context_set_update_callback(m_renderContext, &MpvCore::renderUpdateCb, this);

    // Tell the item the render context is ready so it can issue the pending load.
    QMetaObject::invokeMethod(this, [this] {
        emit renderContextReady();
    }, Qt::QueuedConnection);
    return true;
}

void MpvCore::freeRenderContext()
{
    if (!m_renderContext)
        return;
    mpv_render_context_set_update_callback(m_renderContext, nullptr, nullptr);
    mpv_render_context_free(m_renderContext);
    m_renderContext = nullptr;
}

// ---------------------------------------------------------------------------

namespace {
class MpvRenderer final : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(std::shared_ptr<MpvCore> core)
        : m_core(std::move(core))
    {
    }

    ~MpvRenderer() override
    {
        // Renderer is destroyed on the render thread; free the GL-backed render
        // context here so it always precedes mpv_destroy() in ~MpvCore().
        m_core->freeRenderContext();
    }

    void render() override
    {
        QOpenGLContext *context = QOpenGLContext::currentContext();
        if (!context)
            return;
        QOpenGLFunctions *gl = context->functions();
        gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        if (!m_core->createRenderContextIfNeeded())
            return;
        if (!m_core->hasRenderContext())
            return;

        QOpenGLFramebufferObject *fbo = framebufferObject();
        mpv_opengl_fbo mpfbo{
            fbo ? static_cast<int>(fbo->handle()) : 0,
            fbo ? fbo->width() : 1,
            fbo ? fbo->height() : 1,
            0,
        };
        int flipY = 0;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flipY },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };

        mpv_render_context_render(m_core->renderContext(), params);
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    std::shared_ptr<MpvCore> m_core;
};
} // namespace

MpvPlayerNative::MpvPlayerNative(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
    , m_core(std::make_shared<MpvCore>())
{
    connect(m_core.get(), &MpvCore::eventsProcessed,
            this, &MpvPlayerNative::onEventsProcessed);
    connect(m_core.get(), &MpvCore::fileLoaded,
            this, &MpvPlayerNative::onFileLoaded);
    connect(m_core.get(), &MpvCore::playbackEndFile,
            this, &MpvPlayerNative::onPlaybackEndFile);
    connect(m_core.get(), &MpvCore::errorMessage,
            this, &MpvPlayerNative::onErrorMessage);
    connect(m_core.get(), &MpvCore::renderContextReady,
            this, &MpvPlayerNative::onRenderContextReady);
    connect(m_core.get(), &MpvCore::renderContextFailed,
            this, &MpvPlayerNative::onRenderContextFailed);
    connect(m_core.get(), &MpvCore::frameNeedsUpdate,
            this, &MpvPlayerNative::onFrameNeedsUpdate);

    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setInterval(5000);
    connect(m_playbackTimer, &QTimer::timeout,
            this, &MpvPlayerNative::onPlaybackTimerTick);

    if (!m_core->initialize())
        setError(m_core->initError());
}

MpvPlayerNative::~MpvPlayerNative()
{
    stopPlaybackTimer();
    m_pendingVideoId.clear();
    if (m_core) {
        m_core->disconnect(this);
        // Stop event dispatch (GUI) and clear the wakeup callback before the
        // render thread frees the render context and destroys the mpv core.
        m_core->shutdown();
    }
    m_core.reset();
}

QQuickFramebufferObject::Renderer *MpvPlayerNative::createRenderer() const
{
    if (QQuickWindow *w = window()) {
        // Keep the graphics context and scene graph alive so the
        // mpv_render_context, which holds GL function pointers, stays valid
        // across frames.
        w->setPersistentGraphics(true);
        w->setPersistentSceneGraph(true);
    }
    return new MpvRenderer(m_core);
}

void MpvPlayerNative::setVideoId(QString videoId)
{
    if (m_videoId == videoId)
        return;
    m_videoId = std::move(videoId);
    emit videoIdChanged();
    requestLoad();
}

void MpvPlayerNative::setStartSeconds(int startSeconds)
{
    if (m_startSeconds == startSeconds)
        return;
    m_startSeconds = startSeconds;
    emit startSecondsChanged();
}

void MpvPlayerNative::setMaximumVideoHeight(int maximumVideoHeight)
{
    if (m_maximumVideoHeight == maximumVideoHeight)
        return;
    m_maximumVideoHeight = maximumVideoHeight;
    emit maximumVideoHeightChanged();
    if (!isValidVideoId(m_videoId))
        return;
    if (!m_core || !m_core->initialized() || m_renderFailed)
        return;
    if (!m_renderReady) {
        // Pending load will pick up new quality when ready.
        return;
    }
    int start = m_startSeconds;
    if (m_fileLoaded && !m_ended && std::isfinite(m_position))
        start = qMax(0, static_cast<int>(m_position));
    issueLoad(m_videoId, start);
}

void MpvPlayerNative::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit pausedChanged();
    if (m_core)
        m_core->setPaused(paused);
}

void MpvPlayerNative::setVolume(int volume)
{
    volume = qBound(0, volume, 100);
    if (m_volume == volume)
        return;
    m_volume = volume;
    emit volumeChanged();
    if (m_core)
        m_core->setVolume(volume);
}

void MpvPlayerNative::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    emit mutedChanged();
    if (m_core)
        m_core->setMuted(muted);
}

void MpvPlayerNative::togglePaused()
{
    setPaused(!m_paused);
}

void MpvPlayerNative::seek(double seconds)
{
    if (m_core)
        m_core->seek(seconds);
    m_position = seconds;
    emit positionChanged();
    if (m_ended) {
        // Seeking after end restarts playback and resumes position reports.
        setEnded(false);
        startPlaybackTimer();
    }
    // Immediately resynchronize WatchTracker without crediting seek distance.
    emit playbackUpdated(seconds, false);
}

void MpvPlayerNative::stop()
{
    stopPlaybackTimer();
    m_pendingVideoId.clear();
    if (m_core)
        m_core->stop();
    m_fileLoaded = false;
    setLoading(false);
    setEnded(false);
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged();
    emit durationChanged();
}

bool MpvPlayerNative::isValidVideoId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return re.match(id).hasMatch();
}

void MpvPlayerNative::requestLoad()
{
    if (!isValidVideoId(m_videoId)) {
        setError(QStringLiteral("Invalid YouTube video id."));
        m_pendingVideoId.clear();
        return;
    }
    if (!m_core || !m_core->initialized()) {
        setError(m_core ? m_core->initError() : QStringLiteral("mpv is unavailable."));
        m_pendingVideoId.clear();
        return;
    }
    if (m_renderFailed) {
        setError(QStringLiteral("Video rendering is unavailable."));
        m_pendingVideoId.clear();
        return;
    }
    if (!m_renderReady) {
        m_pendingVideoId = m_videoId;
        return;
    }
    issueLoad(m_videoId, m_startSeconds);
}

void MpvPlayerNative::issueLoad(const QString &id, int startSeconds)
{
    m_fileLoaded = false;
    applyPlaybackOptions(startSeconds);
    setError(QString());
    setEnded(false);
    setLoading(true);
    if (m_core)
        m_core->loadUrl(QUrl(QStringLiteral("https://www.youtube.com/watch?v=") + id));
}

void MpvPlayerNative::applyPlaybackOptions(int startSeconds)
{
    if (!m_core)
        return;
    const int height = PlaybackSettings::normalizeMaximumVideoHeight(m_maximumVideoHeight);
    const QString format = PlaybackSettings::ytDlpFormatForMaximumHeight(height);
    if (format.isEmpty())
        m_core->clearYtdlFormat();
    else
        m_core->setYtdlFormat(format);
    m_core->setStartTime(startSeconds);
}

void MpvPlayerNative::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MpvPlayerNative::setEnded(bool ended)
{
    if (m_ended == ended)
        return;
    m_ended = ended;
    emit endedChanged();
}

void MpvPlayerNative::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

void MpvPlayerNative::startPlaybackTimer()
{
    m_playbackTimer->start();
}

void MpvPlayerNative::stopPlaybackTimer()
{
    m_playbackTimer->stop();
}

void MpvPlayerNative::onEventsProcessed()
{
    const double position = m_core ? m_core->position() : 0.0;
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
    const double duration = m_core ? m_core->duration() : 0.0;
    if (duration != m_duration) {
        m_duration = duration;
        emit durationChanged();
    }
    const bool paused = m_core ? m_core->paused() : false;
    if (paused != m_paused) {
        m_paused = paused;
        emit pausedChanged();
    }
    const int volume = m_core ? m_core->volume() : 0;
    if (volume != m_volume) {
        m_volume = volume;
        emit volumeChanged();
    }
    const bool muted = m_core ? m_core->muted() : false;
    if (muted != m_muted) {
        m_muted = muted;
        emit mutedChanged();
    }
    const bool buffering = m_core && m_core->buffering();
    if (buffering != m_buffering) {
        m_buffering = buffering;
        emit loadingChanged();
    }
}

void MpvPlayerNative::onFileLoaded()
{
    m_fileLoaded = true;
    setError(QString());
    setEnded(false);
    setLoading(false);
    startPlaybackTimer();
}

void MpvPlayerNative::onPlaybackEndFile(int reason, int error)
{
    if (reason == MPV_END_FILE_REASON_EOF) {
        if (m_loading)
            return; // previous file ending while a replacement loads
        setEnded(true);
        setLoading(false);
        stopPlaybackTimer();
        emit playbackUpdated(m_position, false);
    } else if (reason == MPV_END_FILE_REASON_ERROR) {
        setError(QString::fromUtf8(mpv_error_string(error)));
        setEnded(true);
        setLoading(false);
        stopPlaybackTimer();
    } else if (reason == MPV_END_FILE_REASON_STOP) {
        if (m_loading)
            return; // previous file replaced by a new load
        setEnded(false);
        setLoading(false);
        stopPlaybackTimer();
    }
}

void MpvPlayerNative::onErrorMessage(const QString &message)
{
    setError(message);
}

void MpvPlayerNative::onRenderContextReady()
{
    m_renderReady = true;
    if (!m_pendingVideoId.isEmpty()) {
        const QString id = m_pendingVideoId;
        m_pendingVideoId.clear();
        issueLoad(id, m_startSeconds);
    }
}

void MpvPlayerNative::onRenderContextFailed(const QString &message)
{
    m_renderFailed = true;
    setError(message);
    m_pendingVideoId.clear();
    setLoading(false);
}

void MpvPlayerNative::onFrameNeedsUpdate()
{
    update();
}

void MpvPlayerNative::onPlaybackTimerTick()
{
    const double position = m_core ? m_core->position() : 0.0;
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
    const bool playing = !m_paused && !loading();
    emit playbackUpdated(position, playing);
}

#include "mpvplayer.moc"
