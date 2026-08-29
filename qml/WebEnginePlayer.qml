import QtQuick
import QtWebEngine

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    property bool speedBoostActive: false

    function syncSpeedBoost() {
        if (speedBoostActive)
            player.runJavaScript("if (window.__omaStartSpeedBoost) window.__omaStartSpeedBoost();")
        else
            player.runJavaScript("if (window.__omaStopSpeedBoost) window.__omaStopSpeedBoost();")
    }

    function startSpeedBoost() {
        if (speedBoostActive)
            return
        speedBoostActive = true
        syncSpeedBoost()
    }

    function stopSpeedBoost() {
        if (!speedBoostActive)
            return
        speedBoostActive = false
        syncSpeedBoost()
    }

    function togglePaused() {
        player.runJavaScript("if (window.__omaTogglePaused) window.__omaTogglePaused(); else { try { var s = omaPlayer.getPlayerState(); if (s === 1) omaPlayer.pauseVideo(); else omaPlayer.playVideo(); } catch(e) {} }")
    }

    function playerHtml(id) {
        const encodedId = encodeURIComponent(id)
        const startAt = root.startSeconds > 0 ? Math.floor(root.startSeconds) : 0
        const boostActive = root.speedBoostActive ? "true" : "false"
        return "<!doctype html><html><head><meta charset=\"utf-8\">"
            + "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
            + "<style>html,body{width:100%;height:100%;margin:0;border:0;overflow:hidden;"
            + "background:#000}#player{width:100%;height:100%}</style></head><body>"
            + "<div id=\"player\"></div>"
            + "<script src=\"https://www.youtube.com/iframe_api\"></script>"
            + "<script>"
            + "var omaPlayer = null;"
            + "var omaPendingId = '" + encodedId + "';"
            + "var omaSpeedBoostActive = " + boostActive + ";"
            + "var omaSpeedBoostApplied = false;"
            + "var omaSavedRate = 1;"
            + "function omaApplySpeedBoost() {"
            + "  if (!omaSpeedBoostActive) return;"
            + "  if (!omaPlayer || !omaPlayer.getPlaybackRate || !omaPlayer.setPlaybackRate) return;"
            + "  if (omaSpeedBoostApplied) return;"
            + "  try { var r = omaPlayer.getPlaybackRate();"
            + "    if (typeof r === 'number' && isFinite(r) && r>0) omaSavedRate = r; } catch(e) {}"
            + "  try { omaPlayer.setPlaybackRate(2); } catch(e) {}"
            + "  omaSpeedBoostApplied = true;"
            + "}"
            + "window.onYouTubeIframeAPIReady = function() {"
            + "  omaPlayer = new YT.Player('player', {"
            + "    videoId: decodeURIComponent(omaPendingId),"
            + "    playerVars: {autoplay: 1, playsinline: 1, rel: 0, start: " + startAt + "},"
            + "    events: {onReady: function(e) { omaPlayer = e.target; omaApplySpeedBoost(); }}"
            + "  });"
            + "};"
            + "window.__omaStartSpeedBoost = function() {"
            + "  omaSpeedBoostActive = true;"
            + "  omaApplySpeedBoost();"
            + "};"
            + "window.__omaStopSpeedBoost = function() {"
            + "  omaSpeedBoostActive = false;"
            + "  if (!omaSpeedBoostApplied) return;"
            + "  if (!omaPlayer || !omaPlayer.setPlaybackRate) { omaSpeedBoostApplied = false; return; }"
            + "  var restore = (typeof omaSavedRate === 'number' && isFinite(omaSavedRate) && omaSavedRate>0) ? omaSavedRate : 1;"
            + "  try { omaPlayer.setPlaybackRate(restore); } catch(e) {}"
            + "  omaSpeedBoostApplied = false;"
            + "};"
            + "window.__omaTogglePaused = function() {"
            + "  try { var s = omaPlayer.getPlayerState(); if (s === 1) omaPlayer.pauseVideo(); else omaPlayer.playVideo(); } catch(e) {}"
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

        onLoadingChanged: function(loadRequest) {
            root.syncSpeedBoost()
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

    Component.onDestruction: {
        if (speedBoostActive)
            stopSpeedBoost()
        player.stop()
    }
}
