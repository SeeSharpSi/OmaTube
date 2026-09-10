import QtQuick
import QtQuick.Controls
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

    Timer {
        interval: 500
        repeat: true
        running: root.videoId.length > 0 && App.sponsorBlockEnabled
            && !player.paused && !player.loading
        onTriggered: {
            const t = App.sponsorSkipTarget(player.position)
            if (t >= 0)
                player.seek(t)
        }
    }

    property var manualSegment: App.sponsorBlockEnabled
        ? App.sponsorManualSegmentAt(player.position) : ({})

    Button {
        objectName: "sponsorSkipButton"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.bottomMargin: 110
        visible: root.manualSegment && root.manualSegment.category !== undefined
        z: 10
        text: qsTr("Skip %1").arg(root.sponsorLabel(root.manualSegment.category))
        onClicked: {
            if (root.manualSegment && root.manualSegment.end)
                player.seek(root.manualSegment.end + 0.1)
        }

        PointingCursor {}

        background: Rectangle {
            color: App.themeColors.background
            border.color: App.themeColors[
                App.sponsorColorKey(root.manualSegment.category)] ?? App.themeColors.green
            border.width: 2
        }

        contentItem: Text {
            text: parent.text
            color: App.themeColors.foreground
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.weight: Font.DemiBold
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
