pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import YtClient

ApplicationWindow {
    id: root

    readonly property var themeColors: App.themeColors
    readonly property color accent: themeColors.accent
    readonly property color ink: themeColors.foreground
    readonly property color mutedInk: themeColors.dark_foreground
    readonly property color paper: themeColors.background
    readonly property color panel: themeColors.lighter_background
    readonly property color rule: themeColors.muted
    readonly property color softFill: themeColors.selection
    readonly property color danger: themeColors.red
    readonly property color liveRed: themeColors.bright_red
    readonly property color neonYellow: themeColors.bright_yellow
    readonly property color errorFill: Qt.tint(
        paper, Qt.rgba(danger.r, danger.g, danger.b, 0.14))
    readonly property color errorBorder: Qt.tint(
        paper, Qt.rgba(danger.r, danger.g, danger.b, 0.48))
    property bool historyOpen: false
    property bool modalOpen: settingsDialog.visible || App.playerOpen || root.historyOpen
    property int spinnerFrame: 0
    readonly property var spinnerFrames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]

    Keybinds {
        id: keybinds
    }

    width: 900
    height: 820
    minimumWidth: 620
    minimumHeight: 540
    visible: true
    title: App.playerOpen && App.currentVideoTitle.length > 0
        ? App.currentVideoTitle + " - OmaTube"
        : qsTr("OmaTube")
    color: paper
    palette.window: paper
    palette.windowText: ink
    palette.base: panel
    palette.alternateBase: softFill
    palette.text: ink
    palette.button: softFill
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: panel
    palette.mid: rule
    palette.placeholderText: mutedInk

    function relativeTime(value) {
        const seconds = Math.max(0, Math.floor((Date.now() - value.getTime()) / 1000))
        if (seconds < 60)
            return qsTr("now")
        const minutes = Math.floor(seconds / 60)
        if (minutes < 60)
            return qsTr("%1 min ago").arg(minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("%1 hr ago").arg(hours)
        const days = Math.floor(hours / 24)
        if (days < 30)
            return qsTr("%1 days ago").arg(days)
        return Qt.formatDate(value, "MMM d, yyyy")
    }

    Component.onCompleted: App.startupRefresh()

    Timer {
        interval: 80
        running: App.refreshing || App.historyLoading
        repeat: true
        onTriggered: root.spinnerFrame = (root.spinnerFrame + 1) % root.spinnerFrames.length
    }

    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: !settingsDialog.visible && !App.playerOpen && !App.refreshing
        onActivated: App.refresh()
    }

    Shortcut {
        sequence: "S"
        context: Qt.WindowShortcut
        enabled: !settingsDialog.visible && !App.playerOpen
        onActivated: settingsDialog.open()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: App.playerOpen || root.historyOpen
        onActivated: {
            if (root.historyOpen)
                root.historyOpen = false
            else if (root.visibility === Window.FullScreen)
                root.showNormal()
            else
                App.closePlayer()
        }
    }

    Shortcut {
        sequence: "H"
        context: Qt.WindowShortcut
        enabled: !root.modalOpen && !App.refreshing
        onActivated: {
            App.reloadWatchHistory()
            root.historyOpen = true
        }
    }

    Shortcut {
        sequence: "q"
        context: Qt.WindowShortcut
        onActivated: Qt.quit()
    }

    Shortcut {
        sequence: "j"
        context: Qt.WindowShortcut
        enabled: !settingsDialog.visible && !App.playerOpen
        onActivated: {
            if (root.historyOpen) {
                if (historyLoader.item)
                    historyLoader.item.scrollBy(120)
                return
            }
            const maxY = Math.max(0, feedList.contentHeight - feedList.height)
            feedList.contentY = Math.min(maxY, feedList.contentY + 120)
        }
    }

    Shortcut {
        sequence: "k"
        context: Qt.WindowShortcut
        enabled: !settingsDialog.visible && !App.playerOpen
        onActivated: {
            if (root.historyOpen) {
                if (historyLoader.item)
                    historyLoader.item.scrollBy(-120)
                return
            }
            feedList.contentY = Math.max(0, feedList.contentY - 120)
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 28
        width: Math.min(parent.width - 56, 820)
        spacing: 18
        visible: !App.playerOpen && !root.historyOpen

        ColumnLayout {
            Layout.fillWidth: true
            visible: liveList.count > 0
            spacing: 9

            Label {
                text: qsTr("LIVE NOW")
                color: root.liveRed
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.5
            }

            ListView {
                id: liveList
                Layout.fillWidth: true
                implicitHeight: 88
                orientation: ListView.Horizontal
                spacing: 10
                clip: true
                model: App.liveChannels
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: liveDelegate
                    required property string channelTitle
                    required property string videoId
                    required property string videoTitle

                    width: 72
                    height: 86

                    Column {
                        width: parent.width
                        spacing: 5

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 58
                            height: 58
                            radius: 29
                            color: liveHover.hovered
                                ? Qt.tint(root.panel, Qt.rgba(
                                    root.liveRed.r, root.liveRed.g, root.liveRed.b, 0.14))
                                : root.panel
                            border.width: 2
                            border.color: root.liveRed

                            Text {
                                anchors.centerIn: parent
                                text: liveDelegate.channelTitle.length > 0
                                    ? liveDelegate.channelTitle.charAt(0).toUpperCase()
                                    : "?"
                                color: root.ink
                                font.pixelSize: 20
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
                    HoverHandler {
                        id: liveHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    ToolTip.visible: liveHover.hovered
                    ToolTip.text: liveDelegate.videoTitle
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            implicitHeight: 42
            contentWidth: categoryRow.implicitWidth
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
                        leftPadding: 18
                        rightPadding: 18
                        text: categoryButton.name
                        flat: true
                        onClicked: App.selectCategory(categoryButton.categoryId)

                        PointingCursor {}

                        background: Rectangle {
                            color: App.selectedCategoryId === categoryButton.categoryId
                                ? root.accent : "transparent"
                            border.color: App.selectedCategoryId === categoryButton.categoryId
                                ? root.accent : root.rule
                        }

                        contentItem: Text {
                            text: categoryButton.text
                            color: App.selectedCategoryId === categoryButton.categoryId
                                ? root.panel : root.ink
                            font.pixelSize: 14
                            font.weight: App.selectedCategoryId === categoryButton.categoryId
                                ? Font.DemiBold : Font.Normal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.rule
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + 20
            color: root.errorFill
            border.color: root.errorBorder
            visible: App.errorMessage.length > 0

            Text {
                id: errorText
                anchors.fill: parent
                anchors.margins: 10
                text: App.errorMessage
                color: root.danger
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            TapHandler {
                cursorShape: Qt.PointingHandCursor
                onTapped: App.clearError()
            }
        }

        ListView {
            id: feedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.feed
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 800

            onContentYChanged: {
                if (App.historyLoading || !App.historyHasMore)
                    return
                if (contentY + height >= contentHeight - 600)
                    App.loadMoreHistory()
            }
            onCountChanged: {
                if (App.historyLoading || !App.historyHasMore)
                    return
                if (contentHeight <= height)
                    App.loadMoreHistory()
            }

            footer: Item {
                width: feedList.width
                height: visible ? 52 : 0
                visible: App.historyLoading || App.historyHasMore

                Label {
                    anchors.centerIn: parent
                    color: root.mutedInk
                    font.pixelSize: 12
                    text: App.historyLoading
                          ? root.spinnerFrames[root.spinnerFrame]
                          : (feedList.count > 0 ? qsTr("Scroll for more") : "")
                }
            }

            delegate: Item {
                id: feedDelegate
                required property string videoId
                required property string channelTitle
                required property string title
                required property date publishedAt
                required property int watchProgressPercent

                width: ListView.view.width
                height: feedColumn.implicitHeight + 34

                Column {
                    id: feedColumn
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: feedDelegate.watchProgressPercent >= 0 ? 56 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Text {
                        width: parent.width
                        text: feedDelegate.title
                        color: root.ink
                        font.pixelSize: 21
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        text: feedDelegate.channelTitle + "  \u00b7  "
                              + root.relativeTime(feedDelegate.publishedAt)
                        color: root.mutedInk
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: feedDelegate.watchProgressPercent >= 0
                    text: qsTr("%1%").arg(feedDelegate.watchProgressPercent)
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
                    color: feedHover.hovered ? root.softFill : "transparent"
                }

                HoverHandler {
                    id: feedHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler { onTapped: App.openVideo(feedDelegate.videoId) }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 430)
                spacing: 8
                visible: feedList.count === 0 && !App.refreshing

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: root.ink
                    font.pixelSize: 19
                    font.weight: Font.Medium
                    text: App.apiKeyConfigured
                        ? qsTr("No videos yet")
                        : qsTr("Set up OmaTube")
                }

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: root.mutedInk
                    font.pixelSize: 13
                    text: App.apiKeyConfigured
                        ? qsTr("Add a channel in Settings, then refresh.")
                        : qsTr("Open Settings to add your YouTube API key and channels.")
                }
            }
        }

    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 18
        anchors.topMargin: 18
        z: 10
        width: refreshRow.implicitWidth + 16
        height: 32
        visible: App.refreshing && !App.playerOpen
        color: root.paper
        border.color: root.rule

        Row {
            id: refreshRow
            anchors.centerIn: parent
            spacing: 7

            Text {
                text: root.spinnerFrames[root.spinnerFrame]
                color: root.ink
                font.family: "monospace"
                font.pixelSize: 16
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: App.progressText.length > 0 ? App.progressText : qsTr("Refreshing...")
                color: root.mutedInk
                font.pixelSize: 12
            }
        }
    }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        z: 10
        visible: !App.playerOpen && !root.historyOpen
        color: root.mutedInk
        font.pixelSize: 11
        text: keybinds.footerText("feed")
    }

    SettingsDialog {
        id: settingsDialog
        parent: root.contentItem
    }

    Loader {
        id: historyLoader
        anchors.fill: parent
        z: 15
        active: root.historyOpen
        source: "qrc:/qml/HistoryPage.qml"

        onLoaded: {
            item.keybinds = keybinds
            item.videoSelected.connect(function(videoId) {
                root.historyOpen = false
                App.openVideo(videoId)
            })
        }
    }

    Loader {
        id: playerLoader
        anchors.fill: parent
        z: 20
        active: App.playerOpen
        source: "qrc:/qml/VideoPlayerPage.qml"

        onLoaded: {
            item.hostWindow = root
            // Bind start position before videoId: the videoId binding is
            // evaluated immediately and generates the embed URL.
            if (item.hasOwnProperty("maximumVideoHeight"))
                item.maximumVideoHeight = Qt.binding(
                    function() { return App.currentVideoMaximumHeight })
            if (item.hasOwnProperty("startSeconds"))
                item.startSeconds = Qt.binding(function() { return App.currentStartPosition })
            item.videoId = Qt.binding(function() { return App.currentVideoId })
        }
    }
}
