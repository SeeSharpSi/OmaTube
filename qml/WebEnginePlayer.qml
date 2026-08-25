import QtQuick
import QtWebEngine

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    function playerHtml(id) {
        const encodedId = encodeURIComponent(id)
        const startAt = root.startSeconds > 0 ? Math.floor(root.startSeconds) : 0
        return "<!doctype html><html><head><meta charset=\"utf-8\">"
            + "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
            + "<style>html,body{width:100%;height:100%;margin:0;border:0;overflow:hidden;"
            + "background:#000}#player{width:100%;height:100%}</style></head><body>"
            + "<div id=\"player\"></div>"
            + "<script src=\"https://www.youtube.com/iframe_api\"></script>"
            + "<script>"
            + "var omaPlayer = null;"
            + "var omaPendingId = '" + encodedId + "';"
            + "window.onYouTubeIframeAPIReady = function() {"
            + "  omaPlayer = new YT.Player('player', {"
            + "    videoId: decodeURIComponent(omaPendingId),"
            + "    playerVars: {autoplay: 1, playsinline: 1, rel: 0, start: " + startAt + "},"
            + "    events: {onReady: function(e) { omaPlayer = e.target; }}"
            + "  });"
            + "};"
            + "window.__omaPlaybackReport = function() {"
            + "  try {"
            + "    if (!omaPlayer || !omaPlayer.getPlayerState) return null;"
            + "    var s = omaPlayer.getPlayerState();"
            + "    var t = omaPlayer.getCurrentTime();"
            + "    if (typeof t !== 'number' || isNaN(t)) return null;"
            + "    return JSON.stringify({state: s, time: t});"
            + "  } catch (e) { return null; }"
            + "};"
            + "</script></body></html>"
    }

    WebEngineView {
        id: player
        anchors.fill: parent
        backgroundColor: "black"
        settings.playbackRequiresUserGesture: false
        settings.fullScreenSupportEnabled: true

        onFullScreenRequested: function(request) {
            if (root.hostWindow) {
                if (request.toggleOn)
                    root.hostWindow.showFullScreen()
                else
                    root.hostWindow.showNormal()
            }
            request.accept()
        }

    }

    Timer {
        interval: 5000
        repeat: true
        running: root.videoId.length > 0
        onTriggered: {
            player.runJavaScript(
                "window.__omaPlaybackReport ? window.__omaPlaybackReport() : null",
                function(result) {
                    if (!result || typeof result !== "string")
                        return
                    let report = null
                    try {
                        report = JSON.parse(result)
                    } catch (e) {
                        return
                    }
                    if (!report || typeof report.time !== "number")
                        return
                    root.playbackUpdated(report.time, report.state === 1)
                })
        }
    }

    onVideoIdChanged: {
        if (videoId.length > 0) {
            player.loadHtml(playerHtml(videoId), "https://dev.ytclient.app/")
        } else {
            player.stop()
        }
    }

    Component.onDestruction: player.stop()
}
