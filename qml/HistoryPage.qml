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

        ListView {
            id: historyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.watchHistory
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 800

            delegate: Item {
                id: historyDelegate
                required property string videoId
                required property string channelTitle
                required property string title
                required property date watchedAt
                required property int watchProgressPercent

                width: ListView.view.width
                height: historyColumn.implicitHeight + 34

                Column {
                    id: historyColumn
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: historyDelegate.watchProgressPercent >= 0 ? 56 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Text {
                        width: parent.width
                        text: historyDelegate.title
                        color: root.ink
                        font.pixelSize: 21
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        text: historyDelegate.channelTitle + "  \u00b7  "
                              + qsTr("last viewed")
                              + " " + Qt.formatDateTime(historyDelegate.watchedAt, "MMM d, yyyy h:mm AP")
                        color: root.mutedInk
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: historyDelegate.watchProgressPercent >= 0
                    text: qsTr("%1%").arg(historyDelegate.watchProgressPercent)
                    color: root.neonYellow
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
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
                    color: historyHover.hovered ? root.softFill : "transparent"
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

            Label {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 430)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: root.mutedInk
                font.pixelSize: 13
                visible: historyList.count === 0
                text: qsTr("No watch history yet. Videos you watch appear here.")
            }
        }
    }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        color: root.mutedInk
        font.pixelSize: 11
        text: qsTr("right-click: delete\nesc: feed")
    }
}
