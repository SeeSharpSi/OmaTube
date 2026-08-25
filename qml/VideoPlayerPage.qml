import QtQuick
import YtClient

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    Loader {
        id: backendLoader
        anchors.fill: parent
        source: (Qt.platform.os === "osx" || Qt.platform.os === "macos")
            ? "qrc:/qml/MacVideoPlayer.qml"
            : "qrc:/qml/WebEnginePlayer.qml"

        onLoaded: {
            item.hostWindow = root.hostWindow
            // Bind start position before videoId: the videoId binding is
            // evaluated immediately and triggers the backend load.
            if (item.hasOwnProperty("startSeconds"))
                item.startSeconds = Qt.binding(function() { return root.startSeconds })
            item.videoId = Qt.binding(function() { return root.videoId })
        }
    }

    Connections {
        target: backendLoader.item
        function onPlaybackUpdated(positionSeconds, playing) {
            if (root.videoId.length > 0)
                App.reportPlayback(root.videoId, positionSeconds, playing)
        }
    }
}
