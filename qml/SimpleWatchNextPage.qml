pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Item {
    id: root
    objectName: "watchNextPage"

    signal videoSelected(string videoId)

    property var keybinds

    function scrollBy(delta) {
        const maxY = Math.max(0, watchNextList.contentHeight - watchNextList.height)
        watchNextList.contentY = Math.min(maxY, Math.max(0, watchNextList.contentY + delta))
    }

    readonly property var themeColors: App.themeColors
    readonly property color accent: themeColors.accent
    readonly property color ink: themeColors.foreground
    readonly property color mutedInk: themeColors.dark_foreground
    readonly property color paper: themeColors.background
    readonly property color panel: themeColors.lighter_background
    readonly property color rule: themeColors.muted
    readonly property color softFill: themeColors.selection
    readonly property color neonYellow: themeColors.bright_yellow

    Rectangle {
        anchors.fill: parent
        color: root.paper
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 28
        width: Math.min(parent.width - 56, 820)
        spacing: 18

        Label {
            Layout.fillWidth: true
            text: qsTr("WATCH NEXT (%1/25)").arg(watchNextList.count)
            color: root.neonYellow
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.5
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.rule
        }

        ListView {
            id: watchNextList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.watchNext
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 800

            WheelHandler {
                target: null

                onWheel: (wheel) => {
                    const maxY = Math.max(0, watchNextList.contentHeight - watchNextList.height)
                    const step = wheel.pixelDelta.y !== 0
                        ? wheel.pixelDelta.y * 2
                        : wheel.angleDelta.y / 120 * 120
                    watchNextList.contentY = Math.max(0, Math.min(maxY, watchNextList.contentY - step))
                    wheel.accepted = true
                }
            }

            delegate: Item {
                id: watchNextDelegate
                objectName: "watchNextVideo_" + watchNextDelegate.videoId
                Accessible.role: Accessible.Button
                Accessible.name: "Watch Next video " + watchNextDelegate.videoId + " " + watchNextDelegate.title
                Accessible.onPressAction: root.videoSelected(watchNextDelegate.videoId)
                required property string videoId
                required property string channelTitle
                required property string title
                required property date publishedAt
                required property int watchProgressPercent
                required property int position

                width: ListView.view.width
                height: Math.max(queueColumn.implicitHeight + 34, queueButtons.height + 20)

                Column {
                    id: queueColumn
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: queueButtons.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Text {
                        width: parent.width
                        text: "#" + (watchNextDelegate.position + 1) + "  " + watchNextDelegate.title
                        color: root.ink
                        font.pixelSize: 21
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        text: watchNextDelegate.channelTitle
                              + (watchNextDelegate.watchProgressPercent >= 0
                                 ? "  \u00b7  " + qsTr("%1% watched").arg(watchNextDelegate.watchProgressPercent) : "")
                        color: root.mutedInk
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Row {
                    id: queueButtons
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Button {
                        objectName: "watchNextUp_" + watchNextDelegate.videoId
                        Accessible.name: "Move up " + watchNextDelegate.videoId
                        text: qsTr("\u2191")
                        flat: true
                        enabled: watchNextDelegate.position > 0
                        onClicked: App.moveWatchNext(watchNextDelegate.videoId, watchNextDelegate.position - 1)
                        PointingCursor {}
                    }
                    Button {
                        objectName: "watchNextDown_" + watchNextDelegate.videoId
                        Accessible.name: "Move down " + watchNextDelegate.videoId
                        text: qsTr("\u2193")
                        flat: true
                        enabled: watchNextDelegate.position < watchNextList.count - 1
                        onClicked: App.moveWatchNext(watchNextDelegate.videoId, watchNextDelegate.position + 1)
                        PointingCursor {}
                    }
                    Button {
                        objectName: "watchNextRemove_" + watchNextDelegate.videoId
                        Accessible.name: "Remove from Watch Next " + watchNextDelegate.videoId
                        text: qsTr("\u00d7")
                        flat: true
                        onClicked: App.removeFromWatchNext(watchNextDelegate.videoId)
                        PointingCursor {}
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.rule
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: -12
                    anchors.rightMargin: -12
                    z: -1
                    color: queueHover.hovered ? root.softFill : "transparent"
                }

                HoverHandler {
                    id: queueHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: root.videoSelected(watchNextDelegate.videoId)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: App.removeFromWatchNext(watchNextDelegate.videoId)
                }
            }

            Label {
                anchors.top: parent.top
                anchors.topMargin: 24
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 40, 430)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: root.mutedInk
                font.pixelSize: 13
                visible: watchNextList.count === 0
                text: qsTr("Watch Next is empty. Right-click a feed video to commit it here. Capped at 25 so it stays worth watching.")
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        color: root.panel
        border.color: root.rule
        width: keybindsLabel.implicitWidth + 20
        height: keybindsLabel.implicitHeight + 12

        Label {
            id: keybindsLabel
            anchors.centerIn: parent
            color: root.mutedInk
            font.family: "monospace"
            font.pixelSize: 11
            text: keybinds ? keybinds.footerText("watchnext").split("\n").join("  /  ") : ""
        }
    }
}
