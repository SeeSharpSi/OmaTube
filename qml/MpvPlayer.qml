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

    MpvPlayerNative {
        id: player
        anchors.fill: parent
        videoId: root.videoId
        startSeconds: root.startSeconds
        maximumVideoHeight: root.maximumVideoHeight
        onPlaybackUpdated: function(positionSeconds, playing) {
            root.playbackUpdated(positionSeconds, playing)
        }
    }

    PlayerControls {
        anchors.fill: parent
        player: player
        hostWindow: root.hostWindow
        onCloseRequested: App.closePlayer()
    }

    Component.onDestruction: player.stop()
}
