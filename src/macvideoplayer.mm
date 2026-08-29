#include "macvideoplayer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@interface VideoNavigationDelegate : NSObject<WKNavigationDelegate> {
    MacVideoPlayerNative *_owner;
}
- (instancetype)initWithOwner:(MacVideoPlayerNative *)owner;
@end

@implementation VideoNavigationDelegate

- (instancetype)initWithOwner:(MacVideoPlayerNative *)owner
{
    self = [super init];
    if (self)
        _owner = owner;
    return self;
}

- (void)webView:(WKWebView *)webView
    decidePolicyForNavigationAction:(WKNavigationAction *)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    if (!action.targetFrame) {
        [webView loadRequest:action.request];
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    Q_UNUSED(navigation);
    Q_UNUSED(webView);
    if (_owner)
        _owner->syncSpeedBoost();
}

@end

@interface OmaPlaybackMessageHandler : NSObject<WKScriptMessageHandler>
@end

@implementation OmaPlaybackMessageHandler {
    MacVideoPlayerNative *_owner;
}

- (instancetype)initWithOwner:(MacVideoPlayerNative *)owner
{
    self = [super init];
    if (self)
        _owner = owner;
    return self;
}

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message
{
    Q_UNUSED(userContentController);
    if (!_owner)
        return;
    if (![message.body isKindOfClass:[NSString class]])
        return;
    const QString body = QString::fromNSString(static_cast<NSString *>(message.body));
    const QJsonDocument document = QJsonDocument::fromJson(body.toUtf8());
    if (!document.isObject())
        return;
    const QJsonObject payload = document.object();
    emit _owner->playbackUpdated(
        payload.value(QStringLiteral("time")).toDouble(),
        payload.value(QStringLiteral("state")).toInt() == 1);
}

@end

namespace {

WKWebView *webView(void *value)
{
    return static_cast<WKWebView *>(value);
}

NSView *hostView(QQuickWindow *window)
{
    return window ? reinterpret_cast<NSView *>(window->winId()) : nil;
}

NSURL *playerBaseUrl()
{
    return [NSURL URLWithString:@"https://dev.ytclient.app/"];
}

QString playbackReportJavaScript(bool boostActive)
{
    const QString boostStr = boostActive ? QStringLiteral("true") : QStringLiteral("false");
    return QStringLiteral(
        "var omaPlayer = null;"
        "var omaSpeedBoostActive = %1;"
        "var omaSpeedBoostApplied = false;"
        "var omaSavedRate = 1;"
        "function omaApplySpeedBoost() {"
        "  if (!omaSpeedBoostActive) return;"
        "  if (!omaPlayer || !omaPlayer.getPlaybackRate || !omaPlayer.setPlaybackRate) return;"
        "  if (omaSpeedBoostApplied) return;"
        "  try { var r = omaPlayer.getPlaybackRate();"
        "    if (typeof r === 'number' && isFinite(r) && r>0) omaSavedRate = r; } catch(e) {}"
        "  try { omaPlayer.setPlaybackRate(2); } catch(e) {}"
        "  omaSpeedBoostApplied = true;"
        "}"
        "window.onYouTubeIframeAPIReady = function() {"
        "  omaPlayer = new YT.Player('player', {"
        "    videoId: decodeURIComponent(window.__omaVideoId),"
        "    playerVars: {autoplay: 1, playsinline: 1, rel: 0, start: window.__omaStartSeconds},"
        "    events: {onReady: function(e) { omaPlayer = e.target; omaApplySpeedBoost(); }}"
        "  });"
        "};"
        "window.__omaStartSpeedBoost = function() {"
        "  omaSpeedBoostActive = true;"
        "  omaApplySpeedBoost();"
        "};"
        "window.__omaStopSpeedBoost = function() {"
        "  omaSpeedBoostActive = false;"
        "  if (!omaSpeedBoostApplied) return;"
        "  if (!omaPlayer || !omaPlayer.setPlaybackRate) { omaSpeedBoostApplied = false; return; }"
        "  var restore = (typeof omaSavedRate === 'number' && isFinite(omaSavedRate) && omaSavedRate>0) ? omaSavedRate : 1;"
        "  try { omaPlayer.setPlaybackRate(restore); } catch(e) {}"
        "  omaSpeedBoostApplied = false;"
        "};"
        "window.__omaTogglePaused = function() {"
        "  try { var s = omaPlayer.getPlayerState(); if (s === 1) omaPlayer.pauseVideo(); else omaPlayer.playVideo(); } catch(e) {}"
        "};"
        "window.__omaPlaybackReport = function() {"
        "  try {"
        "    if (!omaPlayer || !omaPlayer.getPlayerState) return null;"
        "    var s = omaPlayer.getPlayerState();"
        "    var t = omaPlayer.getCurrentTime();"
        "    if (typeof t !== 'number' || isNaN(t)) return null;"
        "    return JSON.stringify({state: s, time: t});"
        "  } catch (e) { return null; }"
        "};").arg(boostStr);
}

QString playbackPollerJavaScript()
{
    return QStringLiteral(
        "(function() {"
        "  setInterval(function() {"
        "    try {"
        "      if (!window.__omaPlaybackReport) return;"
        "      var r = window.__omaPlaybackReport();"
        "      if (r && window.webkit && window.webkit.messageHandlers"
        "          && window.webkit.messageHandlers.omaPlayback)"
        "        window.webkit.messageHandlers.omaPlayback.postMessage(r);"
        "    } catch (e) {}"
        "  }, 5000);"
        "})();");
}

QString playerHtml(const QString &videoId, int startSeconds, bool boostActive)
{
    return QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                           "<style>html,body{width:100%;height:100%;margin:0;border:0;"
                           "overflow:hidden;background:#000}#player{width:100%;height:100%}"
                           "</style></head><body>"
                           "<div id=\"player\"></div>"
                           "<script>window.__omaVideoId = '%1';"
                           "window.__omaStartSeconds = %2;</script>"
                           "<script src=\"https://www.youtube.com/iframe_api\"></script>"
                           "<script>%3</script></body></html>")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(videoId)))
        .arg(qMax(0, startSeconds))
        .arg(playbackReportJavaScript(boostActive));
}
}

MacVideoPlayerNative::MacVideoPlayerNative(QQuickItem *parent)
    : QQuickItem(parent)
{
    const auto sync = [this] { syncNativeView(); };
    connect(this, &QQuickItem::windowChanged, this, &MacVideoPlayerNative::setWindow);
    connect(this, &QQuickItem::xChanged, this, sync);
    connect(this, &QQuickItem::yChanged, this, sync);
    connect(this, &QQuickItem::widthChanged, this, sync);
    connect(this, &QQuickItem::heightChanged, this, sync);
    connect(this, &QQuickItem::visibleChanged, this, sync);
}

MacVideoPlayerNative::~MacVideoPlayerNative()
{
    WKWebView *view = webView(m_webView);
    if (view) {
        [view stopLoading];
        [view setNavigationDelegate:nil];
        if (m_messageHandler)
            [view.configuration.userContentController
                removeScriptMessageHandlerForName:@"omaPlayback"];
        [view removeFromSuperview];
        [view release];
    }
    if (m_navigationDelegate)
        [static_cast<VideoNavigationDelegate *>(m_navigationDelegate) release];
    if (m_messageHandler)
        [static_cast<OmaPlaybackMessageHandler *>(m_messageHandler) release];
}

QString MacVideoPlayerNative::videoId() const
{
    return m_videoId;
}

void MacVideoPlayerNative::setVideoId(const QString &videoId)
{
    if (m_videoId == videoId)
        return;
    m_videoId = videoId;
    emit videoIdChanged();
    loadVideo();
}

int MacVideoPlayerNative::startSeconds() const
{
    return m_startSeconds;
}

void MacVideoPlayerNative::setStartSeconds(int startSeconds)
{
    m_startSeconds = qMax(0, startSeconds);
}

void MacVideoPlayerNative::stop()
{
    if (m_speedBoostActive)
        stopSpeedBoost();
    WKWebView *view = webView(m_webView);
    if (!view)
        return;
    [view stopLoading];
    [view loadHTMLString:@"" baseURL:playerBaseUrl()];
}

void MacVideoPlayerNative::evaluateJavaScript(const QString &script)
{
    WKWebView *view = webView(m_webView);
    if (!view)
        return;
    [view evaluateJavaScript:script.toNSString() completionHandler:nil];
}

void MacVideoPlayerNative::syncSpeedBoost()
{
    if (m_speedBoostActive)
        evaluateJavaScript(QStringLiteral("if (window.__omaStartSpeedBoost) window.__omaStartSpeedBoost();"));
    else
        evaluateJavaScript(QStringLiteral("if (window.__omaStopSpeedBoost) window.__omaStopSpeedBoost();"));
}

void MacVideoPlayerNative::startSpeedBoost()
{
    if (m_speedBoostActive)
        return;
    m_speedBoostActive = true;
    syncSpeedBoost();
}

void MacVideoPlayerNative::stopSpeedBoost()
{
    if (!m_speedBoostActive)
        return;
    m_speedBoostActive = false;
    syncSpeedBoost();
}

void MacVideoPlayerNative::togglePaused()
{
    evaluateJavaScript(QStringLiteral("if (window.__omaTogglePaused) window.__omaTogglePaused();"));
}

void MacVideoPlayerNative::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        disconnect(m_window, nullptr, this, nullptr);
    WKWebView *view = webView(m_webView);
    if (view)
        [view removeFromSuperview];
    m_window = window;
    if (m_window) {
        connect(m_window, &QWindow::widthChanged, this, &MacVideoPlayerNative::syncNativeView);
        connect(m_window, &QWindow::heightChanged, this, &MacVideoPlayerNative::syncNativeView);
        connect(m_window, &QWindow::visibleChanged, this, &MacVideoPlayerNative::syncNativeView);
    }
    QTimer::singleShot(0, this, &MacVideoPlayerNative::syncNativeView);
}

void MacVideoPlayerNative::syncNativeView()
{
    if (!m_window)
        return;

    NSView *host = hostView(m_window);
    if (!host)
        return;

    if (!m_webView) {
        WKWebViewConfiguration *configuration = [[WKWebViewConfiguration alloc] init];
        configuration.allowsInlineMediaPlayback = YES;
        configuration.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];

        WKUserScript *poller = [[WKUserScript alloc]
            initWithSource:playbackPollerJavaScript().toNSString()
             injectionTime:WKUserScriptInjectionTimeAtDocumentEnd
          forMainFrameOnly:YES];
        [configuration.userContentController addUserScript:poller];
        [poller release];

        OmaPlaybackMessageHandler *handler =
            [[OmaPlaybackMessageHandler alloc] initWithOwner:this];
        [configuration.userContentController addScriptMessageHandler:handler
                                                                name:@"omaPlayback"];
        m_messageHandler = handler;

        WKWebView *view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
        [configuration release];
        VideoNavigationDelegate *delegate = [[VideoNavigationDelegate alloc] initWithOwner:this];
        view.navigationDelegate = delegate;
        m_navigationDelegate = delegate;
        m_webView = view;
        loadVideo();
    }

    WKWebView *view = webView(m_webView);
    if (!isVisible() || width() <= 0 || height() <= 0) {
        [view removeFromSuperview];
        return;
    }

    const QRectF sceneRect = mapRectToScene(QRectF(0, 0, width(), height()));
    const QPointF topLeft = sceneRect.topLeft();
    const qreal y = [host isFlipped]
        ? topLeft.y()
        : [host bounds].size.height - topLeft.y() - sceneRect.height();
    [view setFrame:CGRectMake(topLeft.x(), y, sceneRect.width(), sceneRect.height())];
    if (view.superview != host)
        [host addSubview:view];
}

void MacVideoPlayerNative::loadVideo()
{
    WKWebView *view = webView(m_webView);
    if (!view) {
        // Preserve boost state for when view is created; HTML generation will honor it.
        return;
    }
    if (m_videoId.isEmpty()) {
        stop();
        return;
    }
    [view loadHTMLString:playerHtml(m_videoId, m_startSeconds, m_speedBoostActive).toNSString()
                  baseURL:playerBaseUrl()];
}
