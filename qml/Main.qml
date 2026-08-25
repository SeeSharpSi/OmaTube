pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

ApplicationWindow {
    id: root

    readonly property color ink: "#1d1b17"
    readonly property color mutedInk: "#6e6960"
    readonly property color paper: "#f5f1e8"
    readonly property color panel: "#fffdf8"
    readonly property color rule: "#d9d2c5"
    readonly property color liveRed: "#b82f2f"
    property bool modalOpen: addChannelDialog.visible || manageDialog.visible || settingsDialog.visible

    width: 980
    height: 720
    minimumWidth: 680
    minimumHeight: 520
    visible: true
    title: qsTr("YT Client")
    color: paper

    Component.onCompleted: App.startupRefresh()

    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: !root.modalOpen && !App.refreshing
        onActivated: App.refresh()
    }

    header: Rectangle {
        implicitHeight: 70
        color: root.panel

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.rule
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 12

            Label {
                text: qsTr("YT CLIENT")
                color: root.ink
                font.pixelSize: 18
                font.weight: Font.Bold
                font.letterSpacing: 1.4
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Add channel")
                onClicked: addChannelDialog.openForCategory(App.selectedCategoryId)
            }

            Button {
                text: qsTr("Manage")
                onClicked: manageDialog.open()
            }

            Button {
                text: qsTr("API key")
                onClicked: settingsDialog.open()
            }

            Button {
                text: App.refreshing ? qsTr("Refreshing") : qsTr("Refresh  R")
                enabled: !App.refreshing
                onClicked: App.refresh()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 24
        anchors.bottomMargin: 18
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: allCategoryButton
                text: qsTr("All")
                flat: true
                font.bold: App.selectedCategoryId < 0
                onClicked: App.selectCategory(-1)

                background: Rectangle {
                    radius: 4
                    color: App.selectedCategoryId < 0 ? root.ink : "transparent"
                    border.color: App.selectedCategoryId < 0 ? root.ink : root.rule
                }

                contentItem: Text {
                    text: allCategoryButton.text
                    color: App.selectedCategoryId < 0 ? root.panel : root.ink
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Flickable {
                Layout.fillWidth: true
                implicitHeight: 40
                contentWidth: categoryRow.width
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: categoryRow
                    height: parent.height
                    spacing: 8

                    Repeater {
                        model: App.categories

                        delegate: Button {
                            id: categoryButton
                            required property var categoryId
                            required property string name

                            height: 40
                            text: categoryButton.name
                            flat: true
                            font.bold: App.selectedCategoryId === categoryButton.categoryId
                            onClicked: App.selectCategory(categoryButton.categoryId)

                            background: Rectangle {
                                radius: 4
                                color: App.selectedCategoryId === categoryButton.categoryId ? root.ink : "transparent"
                                border.color: App.selectedCategoryId === categoryButton.categoryId ? root.ink : root.rule
                            }

                            contentItem: Text {
                                text: categoryButton.text
                                color: App.selectedCategoryId === categoryButton.categoryId ? root.panel : root.ink
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: liveList.count > 0
            spacing: 10

            Label {
                text: qsTr("LIVE NOW")
                color: root.liveRed
                font.pixelSize: 12
                font.weight: Font.Bold
                font.letterSpacing: 1.2
            }

            ListView {
                id: liveList
                Layout.fillWidth: true
                implicitHeight: 98
                orientation: ListView.Horizontal
                spacing: 14
                clip: true
                model: App.liveChannels

                delegate: Item {
                    id: liveDelegate
                    required property string channelTitle
                    required property string videoId
                    required property string videoTitle

                    width: 76
                    height: 96

                    Column {
                        width: parent.width
                        spacing: 6

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 58
                            height: 58
                            radius: 29
                            color: root.panel
                            border.width: 3
                            border.color: root.liveRed

                            Text {
                                anchors.centerIn: parent
                                text: liveDelegate.channelTitle.length > 0
                                    ? liveDelegate.channelTitle.charAt(0).toUpperCase()
                                    : "?"
                                color: root.ink
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                            }
                        }

                        Text {
                            width: parent.width
                            text: liveDelegate.channelTitle
                            color: root.ink
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    TapHandler { onTapped: App.openVideo(liveDelegate.videoId) }
                    HoverHandler { id: liveHover }
                    ToolTip.visible: liveHover.hovered
                    ToolTip.text: liveDelegate.videoTitle
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.rule
            visible: liveList.count > 0
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + 20
            radius: 4
            color: "#f6dddd"
            border.color: "#d79595"
            visible: App.errorMessage.length > 0

            Text {
                id: errorText
                anchors.fill: parent
                anchors.margins: 10
                text: App.errorMessage
                color: "#712222"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            TapHandler { onTapped: App.clearError() }
        }

        ListView {
            id: feedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.feed
            clip: true
            spacing: 0

            delegate: Item {
                id: feedDelegate
                required property string videoId
                required property string channelTitle
                required property string title
                required property date publishedAt

                width: ListView.view.width
                height: feedColumn.implicitHeight + 30

                Column {
                    id: feedColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5

                    Text {
                        width: parent.width
                        text: feedDelegate.channelTitle + "  |  "
                              + Qt.formatDateTime(feedDelegate.publishedAt, "MMM d, h:mm AP")
                        color: root.mutedInk
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: feedDelegate.title
                        color: root.ink
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.rule
                }

                HoverHandler { id: feedHover }
                TapHandler { onTapped: App.openVideo(feedDelegate.videoId) }
                Rectangle {
                    anchors.fill: parent
                    z: -1
                    color: feedHover.hovered ? "#ebe6db" : "transparent"
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 440)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: root.mutedInk
                text: App.apiKeyConfigured
                    ? qsTr("No videos yet. Add channels, then press R.")
                    : qsTr("Add your YouTube Data API key to begin.")
                visible: feedList.count === 0 && !App.refreshing
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: App.refreshing ? App.progressText : App.statusMessage
                color: root.mutedInk
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Label {
                text: App.lastRefreshedAt.getTime() > 0
                    ? qsTr("Last refreshed %1").arg(Qt.formatDateTime(App.lastRefreshedAt, "h:mm AP"))
                    : ""
                color: root.mutedInk
                font.pixelSize: 12
            }
        }
    }

    AddChannelDialog {
        id: addChannelDialog
        parent: root.contentItem
    }

    ManageDialog {
        id: manageDialog
        parent: root.contentItem
    }

    SettingsDialog {
        id: settingsDialog
        parent: root.contentItem
    }
}
