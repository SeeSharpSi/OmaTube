import QtQuick
import YtClient

Item {
    id: root

    property var hostWindow
    property string videoId: ""
    Loader {
        id: backendLoader
        anchors.fill: parent
        source: (Qt.platform.os === "osx" || Qt.platform.os === "macos")
            ? "qrc:/qml/MacVideoPlayer.qml"
            : "qrc:/qml/WebEnginePlayer.qml"

        onLoaded: {
            item.hostWindow = root.hostWindow
            item.videoId = Qt.binding(function() { return root.videoId })
        }
    }
}
