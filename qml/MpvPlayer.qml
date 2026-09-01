import QtQuick
import YtClient

// qmllint disable unresolved-type

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    property int maximumVideoHeight: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    property bool speedBoostActive: false
    property double savedPlaybackRate: 1.0

    function startSpeedBoost() {
        if (speedBoostActive)
            return
        savedPlaybackRate = player.playbackRate
        speedBoostActive = true
        player.playbackRate = 2.0
    }

    function stopSpeedBoost() {
        if (!speedBoostActive)
            return
        speedBoostActive = false
        player.playbackRate = savedPlaybackRate
    }

    function togglePaused() {
        if (!player.loading && !player.ended)
            player.togglePaused()
    }

    MpvPlayerNative {
        id: player
        anchors.fill: parent
        videoId: root.videoId
        startSeconds: root.startSeconds
        maximumVideoHeight: root.maximumVideoHeight
        volume: App.playbackVolume
        onPlaybackUpdated: function(positionSeconds, playing) {
            root.playbackUpdated(positionSeconds, playing)
        }
    }

    Loader {
        anchors.fill: parent
        source: App.simpleUi
            ? "qrc:/qml/SimplePlayerControls.qml"
            : "qrc:/qml/PlayerControls.qml"

        onLoaded: {
            item.player = Qt.binding(function() { return player })
            item.hostWindow = Qt.binding(function() { return root.hostWindow })
            item.closeRequested.connect(function() { App.closePlayer() })
        }
    }

    Component.onDestruction: {
        if (speedBoostActive)
            stopSpeedBoost()
        player.stop()
    }
}
