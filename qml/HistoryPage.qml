pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Item {
    id: root

    signal videoSelected(string videoId)

    function scrollBy(delta) {
        const maxY = Math.max(0, historyList.contentHeight - historyList.height)
        historyList.contentY = Math.min(maxY, Math.max(0, historyList.contentY + delta))
    }

    readonly property var themeColors: App.themeColors
    readonly property color accent: themeColors.accent
    readonly property color ink: themeColors.foreground
    readonly property color mutedInk: themeColors.dark_foreground
    readonly property color panel: themeColors.lighter_background
    readonly property color rule: themeColors.muted
    readonly property color softFill: themeColors.selection
    readonly property color neonYellow: themeColors.bright_yellow
    readonly property color glassPanel: Qt.rgba(
        panel.r, panel.g, panel.b, themeColors.mode === "dark" ? 0.78 : 0.88)

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: qsTr("WATCH HISTORY")
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
            id: historyList
            readonly property int columnCount: width >= 1040 ? 4 : width >= 780 ? 3 : 2

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: historyContentColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: historyContentColumn
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right

                GridLayout {
                    id: historyGrid
                    readonly property real cardWidth: Math.max(0,
                        (width - (columns - 1) * columnSpacing) / columns)

                    width: Math.max(0, parent.width - 12)
                    columns: historyList.columnCount
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        id: historyRepeater
                        model: App.watchHistory

                        delegate: Item {
                            id: historyDelegate
                            required property string videoId
                            required property string channelTitle
                            required property string title
                            required property date watchedAt
                            required property int watchProgressPercent

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: historyGrid.cardWidth
                            implicitHeight: historyThumbnail.height + 10 + infoColumn.implicitHeight + 10
                                + (historyDelegate.watchProgressPercent >= 0
                                   ? 5 + watchProgress.implicitHeight : 0)

                            Rectangle {
                                anchors.fill: parent
                                color: root.glassPanel
                                border.color: historyHover.hovered ? root.accent : root.rule
                                border.width: historyHover.hovered ? 2 : 1

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
                                    id: historyThumbnail
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: width * 0.5625
                                    source: "https://i.ytimg.com/vi/" + historyDelegate.videoId + "/hqdefault.jpg"
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: parent.height - historyThumbnail.height
                                    color: root.glassPanel
                                }

                                Column {
                                    id: infoColumn
                                    anchors.top: historyThumbnail.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.topMargin: 10
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 5

                                    Text { width: parent.width; text: historyDelegate.title; color: root.ink; font.family: "monospace"; font.pixelSize: 14; font.weight: Font.Medium; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }

                                    Text { width: parent.width; text: historyDelegate.channelTitle + "  \u00b7  " + qsTr("last viewed") + " " + Qt.formatDateTime(historyDelegate.watchedAt, "MMM d, yyyy h:mm AP"); color: root.mutedInk; font.family: "monospace"; font.pixelSize: 10; elide: Text.ElideRight }
                                }

                                Text {
                                    id: watchProgress
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    anchors.bottomMargin: 10
                                    text: qsTr("%1% watched").arg(historyDelegate.watchProgressPercent)
                                    color: root.neonYellow
                                    font.family: "monospace"
                                    font.pixelSize: 10
                                    visible: historyDelegate.watchProgressPercent >= 0
                                }

                                Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; height: 3; color: root.neonYellow; visible: historyDelegate.watchProgressPercent >= 0; width: parent.width * Math.max(0, Math.min(100, historyDelegate.watchProgressPercent)) / 100 }
                            }

                            HoverHandler {
                                id: historyHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: root.videoSelected(historyDelegate.videoId)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: App.deleteWatchHistory(historyDelegate.videoId)
                            }
                        }
                    }
                }

                Item { width: 1; height: 18 }
            }

            Label {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 430)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: root.mutedInk
                font.pixelSize: 13
                visible: historyRepeater.count === 0
                text: qsTr("No watch history yet. Videos you watch appear here.")
            }
        }
    }
}
