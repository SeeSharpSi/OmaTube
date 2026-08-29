import QtQuick
import YtClient

// qmllint disable unresolved-type

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    MacVideoPlayerNative {
        id: player
        anchors.fill: parent
        videoId: root.videoId
        startSeconds: root.startSeconds
        onPlaybackUpdated: function(positionSeconds, playing) {
            root.playbackUpdated(positionSeconds, playing)
        }
    }

    function startSpeedBoost() { player.startSpeedBoost() }
    function stopSpeedBoost() { player.stopSpeedBoost() }
    function togglePaused() { player.togglePaused() }

    Component.onDestruction: {
        player.stopSpeedBoost()
        player.stop()
    }
}
