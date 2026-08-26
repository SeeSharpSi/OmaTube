import QtQuick
import YtClient

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    property int maximumVideoHeight: 0

    Loader {
        id: backendLoader
        anchors.fill: parent
        source: (App.videoBackend === "mpv" && App.mpvAvailable)
            ? "qrc:/qml/MpvPlayer.qml"
            : ((Qt.platform.os === "osx" || Qt.platform.os === "macos")
                ? "qrc:/qml/MacVideoPlayer.qml"
                : "qrc:/qml/WebEnginePlayer.qml")

        onLoaded: {
            item.hostWindow = Qt.binding(function() { return root.hostWindow })
            // Bind start position before videoId: the videoId binding is
            // evaluated immediately and triggers the backend load.
            if (item.hasOwnProperty("maximumVideoHeight"))
                item.maximumVideoHeight = Qt.binding(
                    function() { return root.maximumVideoHeight })
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

    onHostWindowChanged: App.pointerWatcher.watch(root.hostWindow)
    Component.onCompleted: App.pointerWatcher.watch(root.hostWindow)
    Component.onDestruction: App.pointerWatcher.stop()
}
