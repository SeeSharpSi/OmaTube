#pragma once

#include <QQuickFramebufferObject>
#include <QString>

#include <memory>

class MpvCore;
class QTimer;

// Native embedded video element backed by libmpv's OpenGL render API rather
// than a native window (no `wid`), a software renderer, an external process,
// or Qt Multimedia. The item owns the playback state / controls on the GUI
// thread; the QQuickFramebufferObject::Renderer owns the mpv_render_context on
// the Qt render thread.
class MpvPlayerNative : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(QString videoId READ videoId WRITE setVideoId NOTIFY videoIdChanged)
    Q_PROPERTY(int startSeconds READ startSeconds WRITE setStartSeconds NOTIFY startSecondsChanged)
    Q_PROPERTY(int maximumVideoHeight READ maximumVideoHeight WRITE setMaximumVideoHeight NOTIFY maximumVideoHeightChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY endedChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit MpvPlayerNative(QQuickItem *parent = nullptr);
    ~MpvPlayerNative() override;

    QString videoId() const { return m_videoId; }
    void setVideoId(QString videoId);

    int startSeconds() const { return m_startSeconds; }
    void setStartSeconds(int startSeconds);

    int maximumVideoHeight() const { return m_maximumVideoHeight; }
    void setMaximumVideoHeight(int maximumVideoHeight);

    bool paused() const { return m_paused; }
    void setPaused(bool paused);

    double position() const { return m_position; }
    double duration() const { return m_duration; }

    int volume() const { return m_volume; }
    void setVolume(int volume);

    bool muted() const { return m_muted; }
    void setMuted(bool muted);

    bool loading() const { return m_loading || m_buffering; }
    bool ended() const { return m_ended; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void togglePaused();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stop();

    Renderer *createRenderer() const override;

signals:
    void videoIdChanged();
    void startSecondsChanged();
    void maximumVideoHeightChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void loadingChanged();
    void endedChanged();
    void errorMessageChanged();
    void playbackUpdated(double positionSeconds, bool playing);

private slots:
    void onEventsProcessed();
    void onFileLoaded();
    void onPlaybackEndFile(int reason, int error);
    void onErrorMessage(const QString &message);
    void onRenderContextReady();
    void onRenderContextFailed(const QString &message);
    void onFrameNeedsUpdate();
    void onPlaybackTimerTick();

private:
    static bool isValidVideoId(const QString &id);
    void requestLoad();
    void issueLoad(const QString &id);
    void applyPlaybackOptions();
    void setLoading(bool loading);
    void setEnded(bool ended);
    void setError(const QString &message);
    void startPlaybackTimer();
    void stopPlaybackTimer();

    std::shared_ptr<MpvCore> m_core;
    QString m_videoId;
    QString m_pendingVideoId;
    int m_startSeconds = 0;
    int m_maximumVideoHeight = 0;
    bool m_renderReady = false;
    bool m_renderFailed = false;
    bool m_paused = false;
    double m_position = 0.0;
    double m_duration = 0.0;
    int m_volume = 0;
    bool m_muted = false;
    bool m_loading = false;
    bool m_buffering = false;
    bool m_ended = false;
    QString m_errorMessage;
    QTimer *m_playbackTimer = nullptr;
};
