import QtQuick
import QtQuick.Controls
import YtClient

// Deterministic automation backend. Never loads media or network content.
// Matches the backend contract used by VideoPlayerPage.
Item {
    id: root
    objectName: "automationPlayer"

    property var hostWindow
    property string videoId: ""
    property int startSeconds: 0
    property int maximumVideoHeight: 0
    signal playbackUpdated(real positionSeconds, bool playing)

    function startSpeedBoost() {}
    function stopSpeedBoost() {}
    function togglePaused() {}

    Rectangle {
        anchors.fill: parent
        color: "black"

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 420)
            spacing: 12

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "white"
                font.pixelSize: 14
                text: "Automation player " + root.videoId
            }

            Button {
                objectName: "playerBackButton"
                Accessible.name: "Back from player"
                Accessible.role: Accessible.Button
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Back")
                onClicked: App.closePlayer()
            }
        }
    }
}
