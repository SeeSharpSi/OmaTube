import QtQuick
import QtWebEngine

Item {
    id: root

    property var hostWindow
    property string videoId: ""

    function playerHtml(id) {
        const encodedId = encodeURIComponent(id)
        return "<!doctype html><html><head><meta charset=\"utf-8\">"
            + "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
            + "<style>html,body,iframe{width:100%;height:100%;margin:0;border:0;overflow:hidden;background:#000}</style>"
            + "</head><body><iframe src=\"https://www.youtube.com/embed/" + encodedId
            + "?autoplay=1&playsinline=1&rel=0\" title=\"YouTube video player\" "
            + "allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture\" "
            + "referrerpolicy=\"strict-origin-when-cross-origin\" allowfullscreen></iframe></body></html>"
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

    onVideoIdChanged: {
        if (videoId.length > 0) {
            player.loadHtml(playerHtml(videoId), "https://dev.ytclient.app/")
        } else {
            player.stop()
        }
    }

    Component.onDestruction: player.stop()
}
