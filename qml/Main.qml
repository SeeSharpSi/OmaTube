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
    readonly property color glassPanel: Qt.rgba(
        panel.r, panel.g, panel.b, themeColors.mode === "dark" ? 0.78 : 0.88)
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

    width: 1180
    height: 780
    minimumWidth: 620
    minimumHeight: 540
    visible: true
    title: App.playerOpen && App.currentVideoTitle.length > 0
        ? App.currentVideoTitle + " - OmaTube"
        : qsTr("OmaTube")
    color: Qt.rgba(paper.r, paper.g, paper.b, 0.88)
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
            feedList.maybeLoadMore()
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
        anchors.topMargin: 20
        anchors.bottomMargin: 30
        width: Math.min(parent.width - 40, 1120)
        spacing: 14
        visible: !App.playerOpen && !root.historyOpen

        RowLayout {
            Layout.fillWidth: true
            implicitHeight: 42

            Column {
                spacing: 1
                Text { text: "OMA / TUBE"; color: root.ink; font.family: "monospace"; font.pixelSize: 16; font.bold: true }
                Text { text: qsTr("PERSONAL VIDEO LIBRARY"); color: root.mutedInk; font.family: "monospace"; font.pixelSize: 8; font.letterSpacing: 1 }
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("FEED"); color: root.accent; font.family: "monospace"; font.pixelSize: 11; font.bold: true }
            Button {
                text: qsTr("HISTORY")
                flat: true
                onClicked: { App.reloadWatchHistory(); root.historyOpen = true }
                contentItem: Text { text: parent.text; color: parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: parent.hovered ? root.softFill : "transparent"; border.color: root.rule }
            }
            Button {
                text: qsTr("SETTINGS")
                flat: true
                onClicked: settingsDialog.open()
                contentItem: Text { text: parent.text; color: parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: parent.hovered ? root.softFill : "transparent"; border.color: root.rule }
            }
            Button {
                text: App.refreshing ? root.spinnerFrames[root.spinnerFrame] : qsTr("REFRESH")
                enabled: !App.refreshing
                flat: true
                onClicked: App.refresh()
                contentItem: Text { text: parent.text; color: parent.hovered ? root.accent : root.ink; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: parent.hovered ? root.softFill : "transparent"; border.color: root.rule }
            }
        }

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
                    required property string avatarUrl

                    width: 72
                    height: 86

                    Column {
                        width: parent.width
                        spacing: 5

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 56
                            height: 56
                            color: liveHover.hovered
                                ? Qt.tint(root.panel, Qt.rgba(
                                    root.liveRed.r, root.liveRed.g, root.liveRed.b, 0.14))
                                : root.panel
                            border.width: 2
                            border.color: root.liveRed
                            clip: true

                            Image {
                                id: avatarImage
                                anchors.fill: parent
                                anchors.margins: 2
                                source: liveDelegate.avatarUrl
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: avatarImage.status !== Image.Ready
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
            id: categoryFlickable
            property Item draggedCategory: null

            function updateCategoryDropIndicator() {
                const dragged = draggedCategory
                if (dragged === null) {
                    categoryDropIndicator.visible = false
                    return
                }

                let nextItem = null
                let priorItem = null
                let rank = 0
                for (let i = 0; i < categoryRepeater.count; ++i) {
                    const item = categoryRepeater.itemAt(i)
                    if (item === null || item === dragged)
                        continue
                    if (rank === dragged.candidateIndex) {
                        nextItem = item
                        break
                    }
                    priorItem = item
                    ++rank
                }

                let lineCenter = 0
                if (nextItem !== null) {
                    lineCenter = categoryRow.x + nextItem.x - categoryRow.spacing / 2
                } else if (priorItem !== null) {
                    lineCenter = categoryRow.x + priorItem.x + priorItem.width
                        + categoryRow.spacing / 2
                } else {
                    categoryDropIndicator.visible = false
                    return
                }

                categoryDropIndicator.x = Math.max(0, Math.min(
                    categoryRow.implicitWidth - categoryDropIndicator.width,
                    lineCenter - categoryDropIndicator.width / 2))
                categoryDropIndicator.visible = true
            }

            Layout.fillWidth: true
            implicitHeight: 36
            contentWidth: categoryRow.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                height: parent.height
                spacing: 8

                Repeater {
                    id: categoryRepeater
                    model: App.categories

                    delegate: Button {
                        id: categoryButton
                        required property var categoryId
                        required property string name
                        required property int index
                        property int candidateIndex: index

                        function updateDropTarget(offsetX) {
                            const draggedCenter = categoryButton.x + categoryButton.width / 2 + offsetX
                            let target = 0
                            for (let i = 0; i < categoryRepeater.count; ++i) {
                                const item = categoryRepeater.itemAt(i)
                                if (item !== null && item !== categoryButton
                                    && item.x + item.width / 2 < draggedCenter)
                                    ++target
                            }
                            categoryButton.candidateIndex = target
                            categoryFlickable.updateCategoryDropIndicator()
                        }

                        height: 34
                        leftPadding: 14
                        rightPadding: 14
                        text: categoryButton.name
                        flat: true
                        onClicked: App.selectCategory(categoryButton.categoryId)

                        transform: Translate {
                            x: categoryDrag.active ? categoryDrag.activeTranslation.x : 0
                        }
                        z: categoryDrag.active ? 1 : 0
                        opacity: categoryDrag.active ? 0.85 : 1

                        DragHandler {
                            id: categoryDrag
                            target: null
                            yAxis.enabled: false

                            onActiveTranslationChanged: {
                                if (!active)
                                    return
                                categoryButton.updateDropTarget(activeTranslation.x)
                            }

                            onActiveChanged: {
                                if (active) {
                                    categoryFlickable.draggedCategory = categoryButton
                                    categoryButton.updateDropTarget(activeTranslation.x)
                                    return
                                }
                                const candidate = categoryButton.candidateIndex
                                categoryFlickable.draggedCategory = null
                                categoryDropIndicator.visible = false
                                if (candidate !== categoryButton.index)
                                    App.moveCategory(categoryButton.categoryId, candidate)
                            }
                        }

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
                            font.family: "monospace"
                            font.pixelSize: 11
                            font.weight: App.selectedCategoryId === categoryButton.categoryId
                                ? Font.DemiBold : Font.Normal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Rectangle {
                id: categoryDropIndicator
                width: 2
                height: 32
                y: 4
                z: 2
                visible: false
                color: root.accent
                radius: 1
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

        GridView {
            id: feedList
            readonly property int columnCount: width >= 1040 ? 4 : width >= 780 ? 3 : 2

            function maybeLoadMore() {
                if (App.historyLoading || !App.historyHasMore)
                    return
                if (contentY + height >= contentHeight - 160)
                    App.loadMoreHistory()
            }

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.feed
            clip: true
            cellWidth: Math.floor(width / columnCount)
            cellHeight: Math.round((cellWidth - 12) * 0.5625) + 112
            leftMargin: 0
            rightMargin: 0
            bottomMargin: 18
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 800

            onMovementEnded: maybeLoadMore()

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
                          : (feedList.count > 0 ? qsTr("Load more") : "")
                }

                TapHandler {
                    enabled: !App.historyLoading && App.historyHasMore
                    cursorShape: Qt.PointingHandCursor
                    onTapped: App.loadMoreHistory()
                }
            }

            delegate: Item {
                id: feedDelegate
                required property string videoId
                required property string channelTitle
                required property string title
                required property date publishedAt
                required property int watchProgressPercent

                width: feedList.cellWidth - 12
                height: feedList.cellHeight - 12

                Rectangle {
                    anchors.fill: parent
                    color: root.glassPanel
                    border.color: feedHover.hovered ? root.accent : root.rule
                    border.width: feedHover.hovered ? 2 : 1

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
                        id: thumbnail
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: width * 0.5625
                        source: "https://i.ytimg.com/vi/" + feedDelegate.videoId + "/hqdefault.jpg"
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height - thumbnail.height
                        color: root.glassPanel
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 5

                        Text { width: parent.width; text: feedDelegate.title; color: root.ink; font.family: "monospace"; font.pixelSize: 14; font.weight: Font.Medium; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }

                        Text { width: parent.width; text: feedDelegate.channelTitle + "  \u00b7  " + root.relativeTime(feedDelegate.publishedAt); color: root.mutedInk; font.family: "monospace"; font.pixelSize: 10; elide: Text.ElideRight }
                        Text { text: qsTr("%1% watched").arg(feedDelegate.watchProgressPercent); color: root.neonYellow; font.family: "monospace"; font.pixelSize: 10; visible: feedDelegate.watchProgressPercent >= 0 }
                    }

                    Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; height: 3; color: root.neonYellow; visible: feedDelegate.watchProgressPercent >= 0; width: parent.width * Math.max(0, Math.min(100, feedDelegate.watchProgressPercent)) / 100 }
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
                    text: qsTr("No videos yet")
                }

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: root.mutedInk
                    font.pixelSize: 13
                    text: qsTr("Add a channel in Settings, then refresh.")
                }
            }
        }

    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 18
        anchors.bottomMargin: 12
        z: 10
        width: refreshRow.implicitWidth + 16
        height: 32
        visible: App.refreshing && !App.playerOpen
        color: root.glassPanel
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
        font.family: "monospace"
        font.pixelSize: 11
        text: keybinds.footerText("feed").split("\n").join("  /  ")
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
            item.closeRequested.connect(function() { root.historyOpen = false })
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
