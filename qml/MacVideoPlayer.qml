import QtQuick
import YtClient

// qmllint disable unresolved-type

Item {
    id: root

    property var hostWindow
    property string videoId: ""

    MacVideoPlayerNative {
        id: player
        anchors.fill: parent
        videoId: root.videoId
    }

    Component.onDestruction: player.stop()
}
