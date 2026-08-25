#include "macvideoplayer.h"

#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@interface VideoNavigationDelegate : NSObject<WKNavigationDelegate>
@end

@implementation VideoNavigationDelegate

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

QString playerHtml(const QString &videoId)
{
    return QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                          "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
                          "<style>html,body,iframe{width:100%;height:100%;margin:0;border:0;"
                          "overflow:hidden;background:#000}</style></head><body>"
                          "<iframe src=\"https://www.youtube.com/embed/%1?autoplay=1&playsinline=1&rel=0\" "
                          "title=\"YouTube video player\" "
                          "allow=\"accelerometer; autoplay; clipboard-write; encrypted-media;"
                          " gyroscope; picture-in-picture\" "
                          "referrerpolicy=\"strict-origin-when-cross-origin\" allowfullscreen>"
                          "</iframe></body></html>")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(videoId)));
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
        [view removeFromSuperview];
        [view release];
    }
    if (m_navigationDelegate)
        [static_cast<VideoNavigationDelegate *>(m_navigationDelegate) release];
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

void MacVideoPlayerNative::stop()
{
    WKWebView *view = webView(m_webView);
    if (!view)
        return;
    [view stopLoading];
    [view loadHTMLString:@"" baseURL:playerBaseUrl()];
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
        WKWebView *view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
        [configuration release];
        VideoNavigationDelegate *delegate = [[VideoNavigationDelegate alloc] init];
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
    if (!view)
        return;
    if (m_videoId.isEmpty()) {
        stop();
        return;
    }
    [view loadHTMLString:playerHtml(m_videoId).toNSString() baseURL:playerBaseUrl()];
}
