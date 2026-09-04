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
    readonly property color glassPanel: Qt.rgba(
        panel.r, panel.g, panel.b, themeColors.mode === "dark" ? 0.78 : 0.88)

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
            text: qsTr("WATCH NEXT (%1/25)").arg(watchNextRepeater.count)
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

        Flickable {
            id: watchNextList
            readonly property int columnCount: width >= 1040 ? 4 : width >= 780 ? 3 : 2

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: watchNextContentColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

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

            Column {
                id: watchNextContentColumn
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right

                GridLayout {
                    id: watchNextGrid
                    readonly property real cardWidth: Math.max(0,
                        (width - (columns - 1) * columnSpacing) / columns)

                    width: Math.max(0, parent.width - 12)
                    columns: watchNextList.columnCount
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        id: watchNextRepeater
                        model: App.watchNext

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

                            Layout.fillWidth: true
                            Layout.preferredWidth: watchNextGrid.cardWidth
                            Layout.preferredHeight: watchNextDelegate.implicitHeight
                            implicitHeight: clickArea.height + queueControls.height + 10

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

                            Rectangle {
                                anchors.fill: parent
                                color: root.glassPanel
                                border.color: queueHover.hovered ? root.accent : root.rule
                                border.width: queueHover.hovered ? 2 : 1

                                Column {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right

                                    Item {
                                        id: clickArea
                                        width: parent.width
                                        height: queueThumbnail.height + 10 + queueInfo.implicitHeight + 10
                                            + (watchNextDelegate.watchProgressPercent >= 0
                                               ? 5 + queueProgress.implicitHeight : 0)

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            height: width * 0.5625
                                            color: root.softFill

                                            Text {
                                                anchors.centerIn: parent
                                                text: "OMA / TUBE"
                                                color: root.mutedInk
                                                font.family: "monospace"
                                                font.pixelSize: 10
                                                font.letterSpacing: 1
                                            }
                                        }

                                        Image {
                                            id: queueThumbnail
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            height: width * 0.5625
                                            source: App.automationMode ? "" : "https://i.ytimg.com/vi/" + watchNextDelegate.videoId + "/hqdefault.jpg"
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: queueThumbnail.bottom
                                            height: parent.height - queueThumbnail.height
                                            color: root.glassPanel
                                        }

                                        Column {
                                            id: queueInfo
                                            anchors.top: queueThumbnail.bottom
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.topMargin: 10
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            spacing: 5

                                            Text { width: parent.width; text: watchNextDelegate.title; color: root.ink; font.family: "monospace"; font.pixelSize: 14; font.weight: Font.Medium; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }

                                            Text { width: parent.width; text: "#" + (watchNextDelegate.position + 1) + "  \u00b7  " + watchNextDelegate.channelTitle; color: root.mutedInk; font.family: "monospace"; font.pixelSize: 10; elide: Text.ElideRight }
                                        }

                                        Text {
                                            id: queueProgress
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            anchors.bottomMargin: 10
                                            text: qsTr("%1% watched").arg(watchNextDelegate.watchProgressPercent)
                                            color: root.neonYellow
                                            font.family: "monospace"
                                            font.pixelSize: 10
                                            visible: watchNextDelegate.watchProgressPercent >= 0
                                        }

                                        Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; height: 3; color: root.neonYellow; visible: watchNextDelegate.watchProgressPercent >= 0; width: parent.width * Math.max(0, Math.min(100, watchNextDelegate.watchProgressPercent)) / 100 }
                                    }

                                    Row {
                                        id: queueControls
                                        width: parent.width
                                        spacing: 6
                                        leftPadding: 10
                                        rightPadding: 10
                                        bottomPadding: 10

                                        Button {
                                            objectName: "watchNextUp_" + watchNextDelegate.videoId
                                            Accessible.name: "Move up " + watchNextDelegate.videoId
                                            Accessible.role: Accessible.Button
                                            text: qsTr("\u2191")
                                            flat: true
                                            enabled: watchNextDelegate.position > 0
                                            onClicked: App.moveWatchNext(watchNextDelegate.videoId, watchNextDelegate.position - 1)
                                            contentItem: Text { text: parent.text; color: parent.enabled ? (parent.hovered ? root.accent : root.ink) : root.mutedInk; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter }
                                            background: Rectangle { color: parent.hovered && parent.enabled ? root.softFill : "transparent"; border.color: root.rule }
                                            PointingCursor {}
                                        }
                                        Button {
                                            objectName: "watchNextDown_" + watchNextDelegate.videoId
                                            Accessible.name: "Move down " + watchNextDelegate.videoId
                                            Accessible.role: Accessible.Button
                                            text: qsTr("\u2193")
                                            flat: true
                                            enabled: watchNextDelegate.position < watchNextRepeater.count - 1
                                            onClicked: App.moveWatchNext(watchNextDelegate.videoId, watchNextDelegate.position + 1)
                                            contentItem: Text { text: parent.text; color: parent.enabled ? (parent.hovered ? root.accent : root.ink) : root.mutedInk; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter }
                                            background: Rectangle { color: parent.hovered && parent.enabled ? root.softFill : "transparent"; border.color: root.rule }
                                            PointingCursor {}
                                        }
                                        Item { width: 6; height: 1 }
                                        Button {
                                            objectName: "watchNextRemove_" + watchNextDelegate.videoId
                                            Accessible.name: "Remove from Watch Next " + watchNextDelegate.videoId
                                            Accessible.role: Accessible.Button
                                            text: qsTr("REMOVE")
                                            flat: true
                                            onClicked: App.removeFromWatchNext(watchNextDelegate.videoId)
                                            contentItem: Text { text: parent.text; color: parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
                                            background: Rectangle { color: parent.hovered ? root.softFill : "transparent"; border.color: root.rule }
                                            PointingCursor {}
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: watchNextRepeater.count === 0 ? 0
                            : (watchNextGrid.columns - (watchNextRepeater.count % watchNextGrid.columns))
                                % watchNextGrid.columns
                        delegate: Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: watchNextGrid.cardWidth
                            Layout.preferredHeight: 0
                            implicitHeight: 0
                        }
                    }
                }

                Item { width: 1; height: 18 }
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
                visible: watchNextRepeater.count === 0
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
