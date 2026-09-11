import QtQuick
import QtWebEngine
import YtClient

Item {
    id: root
    opacity: 1.0

    Rectangle {
        anchors.fill: parent
        color: "black"
        z: -1
    }

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    property bool speedBoostActive: false

    property var sponsorSegments: App.sponsorSegments
    property string sponsorColorsJson: ""

    readonly property var sponsorCategories: [
        "sponsor", "selfpromo", "interaction", "intro", "outro",
        "preview", "music_offtopic", "poi_highlight"
    ]

    function sponsorLabel(category) {
        switch (category) {
        case "sponsor": return qsTr("Sponsor")
        case "selfpromo": return qsTr("Self promotion")
        case "interaction": return qsTr("Interaction reminder")
        case "intro": return qsTr("Intro")
        case "outro": return qsTr("Outro")
        case "preview": return qsTr("Preview / recap")
        case "music_offtopic": return qsTr("Music: non-music")
        case "poi_highlight": return qsTr("Highlight")
        default: return category
        }
    }

    function colorToHex(c) {
        if (!c)
            return "#ffffff"
        const r = Math.round(c.r * 255)
        const g = Math.round(c.g * 255)
        const b = Math.round(c.b * 255)
        return "#" + [r, g, b].map(function(v) {
            return ("0" + v.toString(16)).slice(-2)
        }).join("")
    }

    function sponsorColors() {
        const map = {}
        for (let i = 0; i < sponsorCategories.length; ++i) {
            const key = sponsorCategories[i]
            map[key] = colorToHex(App.themeColors[App.sponsorColorKey(key)])
        }
        sponsorColorsJson = JSON.stringify(map)
        return sponsorColorsJson
    }

    function pushSponsorToJs() {
        if (root.videoId.length === 0)
            return
        const segments = App.sponsorBlockEnabled ? App.sponsorSegments : []
        const actions = App.sponsorActions
        const js = "window.__omaSetSponsor("
            + JSON.stringify(segments) + ","
            + JSON.stringify(actions) + ","
            + JSON.stringify(sponsorColors()) + ");"
        player.runJavaScript(js)
    }

    IframePlaybackSession {
        id: reportSession
        onPlaybackUpdated: function(positionSeconds, playing) {
            root.playbackUpdated(positionSeconds, playing)
        }
    }

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

    function playerHtml(id, loadSession) {
        const encodedId = encodeURIComponent(id)
        const startAt = root.startSeconds > 0 ? Math.floor(root.startSeconds) : 0
        const boostActive = root.speedBoostActive ? "true" : "false"
        const segments = App.sponsorBlockEnabled ? JSON.stringify(App.sponsorSegments) : "[]"
        const actions = JSON.stringify(App.sponsorActions)
        const colors = sponsorColors()
        return "<!doctype html><html><head><meta charset=\"utf-8\">"
            + "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
            + "<style>html,body{width:100%;height:100%;margin:0;border:0;overflow:hidden;"
            + "background:#000}#player{width:100%;height:100%}</style></head><body>"
            + "<div id=\"player\"></div>"
            + "<div id=\"oma-sb-bar\" style=\"position:absolute;left:0;right:0;bottom:0;"
            + "height:5px;pointer-events:none;z-index:10;display:none\"></div>"
            + "<button id=\"oma-sb-skip\" style=\"position:absolute;right:16px;bottom:48px;"
            + "display:none;z-index:10;background:#000;color:#fff;border:2px solid #fff;"
            + "padding:8px 16px;cursor:pointer;font-family:monospace;font-size:12px\"></button>"
            + "<div id=\"oma-sb-tip\" style=\"position:absolute;display:none;z-index:11;"
            + "pointer-events:none;background:#000;color:#fff;border:1px solid #888;"
            + "padding:4px 8px;font-family:monospace;font-size:12px;white-space:nowrap\"></div>"
            + "<script src=\"https://www.youtube.com/iframe_api\"></script>"
            + "<script>"
            + "var omaPlayer = null;"
            + "var omaPendingId = " + JSON.stringify(encodedId) + ";"
            + "var omaLoadSession = " + JSON.stringify(loadSession) + ";"
            + "var omaSpeedBoostActive = " + boostActive + ";"
            + "var omaSpeedBoostApplied = false;"
            + "var omaSavedRate = 1;"
            + "var omaSbSegments = " + segments + ";"
            + "var omaSbActions = " + actions + ";"
            + "var omaSbColors = " + colors + ";"
            + "var omaSbSkipEnd = 0;"
            + "function omaSbLabel(cat) {"
            + "  var m = {sponsor:'Sponsor',selfpromo:'Self promotion',interaction:'Interaction reminder',"
            + "    intro:'Intro',outro:'Outro',preview:'Preview / recap',"
            + "    music_offtopic:'Music: non-music',poi_highlight:'Highlight'};"
            + "  return m[cat] || cat;"
            + "}"
            + "function omaRenderSponsorSegments() {"
            + "  var bar = document.getElementById('oma-sb-bar');"
            + "  if (!bar) return;"
            + "  var tip0 = document.getElementById('oma-sb-tip');"
            + "  if (tip0) tip0.style.display = 'none';"
            + "  bar.innerHTML = '';"
            + "  if (!omaSbSegments || !omaSbSegments.length) { bar.style.display = 'none'; return; }"
            + "  var dur = 0;"
            + "  try { if (omaPlayer && omaPlayer.getDuration) dur = omaPlayer.getDuration(); } catch(e) {}"
            + "  if (!(dur > 0)) { bar.style.display = 'none'; return; }"
            + "  bar.style.display = 'block';"
            + "  for (var i=0;i<omaSbSegments.length;i++) {"
            + "    var s = omaSbSegments[i];"
            + "    if (!s || !s.end || s.end <= s.start) continue;"
            + "    var d = document.createElement('div');"
            + "    d.style.position = 'absolute';"
            + "    d.style.left = ((s.start / dur) * 100) + '%';"
            + "    d.style.width = (((s.end - s.start) / dur) * 100) + '%';"
            + "    d.style.top = '0';"
            + "    d.style.height = '100%';"
            + "    d.style.background = (omaSbColors && omaSbColors[s.category]) ? omaSbColors[s.category] : '#00ff00';"
            + "    d.setAttribute('data-cat', s.category);"
            + "    d.style.pointerEvents = 'auto';"
            + "    d.style.cursor = 'pointer';"
            + "    d.addEventListener('mousemove', function(ev) {"
            + "      var tip = document.getElementById('oma-sb-tip');"
            + "      if (!tip) return;"
            + "      tip.textContent = omaSbLabel(this.getAttribute('data-cat'));"
            + "      tip.style.display = 'block';"
            + "      tip.style.left = (ev.clientX + 12) + 'px';"
            + "      tip.style.top = Math.max(4, ev.clientY - 34) + 'px';"
            + "    });"
            + "    d.addEventListener('mouseleave', function() {"
            + "      var tip = document.getElementById('oma-sb-tip');"
            + "      if (tip) tip.style.display = 'none';"
            + "    });"
            + "    d.addEventListener('click', function(ev) {"
            + "      var dur = 0;"
            + "      try { if (omaPlayer && omaPlayer.getDuration) dur = omaPlayer.getDuration(); } catch(e) {}"
            + "      if (!(dur > 0)) return;"
            + "      var r = bar.getBoundingClientRect();"
            + "      if (!r || !(r.width > 0)) return;"
            + "      var frac = (ev.clientX - r.left) / r.width;"
            + "      if (!(frac >= 0)) frac = 0;"
            + "      if (frac > 1) frac = 1;"
            + "      try { omaPlayer.seekTo(frac * dur, true); } catch(e) {}"
            + "    });"
            + "    bar.appendChild(d);"
            + "  }"
            + "}"
            + "function omaCheckSponsor() {"
            + "  var bar = document.getElementById('oma-sb-bar');"
            + "  if (bar && bar.style.display === 'none') { omaRenderSponsorSegments(); }"
            + "  var skipBtn = document.getElementById('oma-sb-skip');"
            + "  if (!omaSbSegments || !omaSbSegments.length) { if (skipBtn) skipBtn.style.display = 'none'; return; }"
            + "  if (!omaPlayer || !omaPlayer.getCurrentTime) return;"
            + "  var t;"
            + "  try { t = omaPlayer.getCurrentTime(); } catch(e) { return; }"
            + "  if (typeof t !== 'number' || isNaN(t)) return;"
            + "  var found = null;"
            + "  for (var i=0;i<omaSbSegments.length;i++) {"
            + "    var s = omaSbSegments[i];"
            + "    if (s && s.start !== undefined && s.end !== undefined && t >= s.start && t < s.end - 0.15) { found = s; break; }"
            + "  }"
            + "  if (!found) { if (skipBtn) skipBtn.style.display = 'none'; return; }"
            + "  var act = (omaSbActions && omaSbActions[found.category] !== undefined) ? omaSbActions[found.category] : 0;"
            + "  if (act === 2) {"
            + "    try { omaPlayer.seekTo(found.end + 0.1, true); } catch(e) {}"
            + "    return;"
            + "  }"
            + "  if (act === 1) {"
            + "    if (skipBtn) {"
            + "      omaSbSkipEnd = found.end;"
            + "      skipBtn.textContent = 'Skip ' + omaSbLabel(found.category);"
            + "      skipBtn.style.display = 'block';"
            + "    }"
            + "  } else if (skipBtn) {"
            + "    skipBtn.style.display = 'none';"
            + "  }"
            + "}"
            + "document.getElementById('oma-sb-skip').onclick = function() {"
            + "  try { omaPlayer.seekTo(omaSbSkipEnd + 0.1, true); } catch(e) {}"
            + "};"
            + "window.__omaSetSponsor = function(segs, acts, cols) {"
            + "  omaSbSegments = segs;"
            + "  omaSbActions = acts;"
            + "  omaSbColors = cols;"
            + "  omaRenderSponsorSegments();"
            + "};"
            + "setInterval(omaCheckSponsor, 500);"
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
            + "    events: {onReady: function(e) { omaPlayer = e.target; omaApplySpeedBoost(); omaRenderSponsorSegments(); }}"
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
            + "    return JSON.stringify({state: s, time: t, videoId: decodeURIComponent(omaPendingId), loadSession: omaLoadSession});"
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
            root.pushSponsorToJs()
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
                    if (!reportSession || !result || typeof result !== "string")
                        return
                    reportSession.acceptReport(result)
                })
        }
    }

    onVideoIdChanged: {
        if (videoId.length > 0) {
            const loadSession = reportSession.begin(videoId)
            player.loadHtml(playerHtml(videoId, loadSession), "https://dev.ytclient.app/")
        } else {
            reportSession.stop()
            player.stop()
        }
    }

    Connections {
        target: App

        function onSponsorSegmentsChanged() {
            sponsorSegments = App.sponsorSegments
            pushSponsorToJs()
        }

        function onSponsorActionsChanged() {
            pushSponsorToJs()
        }

        function onSponsorBlockEnabledChanged() {
            pushSponsorToJs()
        }

        function onThemeChanged() {
            pushSponsorToJs()
        }
    }

    Component.onDestruction: {
        reportSession.stop()
        if (speedBoostActive)
            stopSpeedBoost()
        player.stop()
    }
}