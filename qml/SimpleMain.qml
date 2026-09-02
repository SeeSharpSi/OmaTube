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
    property bool historyOpen: false
    property bool modalOpen: settingsDialog.visible || App.playerOpen
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
            if (root.historyOpen) {
                root.historyOpen = false
                return
            }
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

                        height: 40
                        leftPadding: 18
                        rightPadding: 18
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
                            font.pixelSize: 14
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

        ListView {
            id: feedList

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
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 800

            onMovementEnded: maybeLoadMore()

            WheelHandler {
                target: null

                onWheel: (wheel) => {
                    const maxY = Math.max(0, feedList.contentHeight - feedList.height)
                    const step = wheel.pixelDelta.y !== 0
                        ? wheel.pixelDelta.y * 2
                        : wheel.angleDelta.y / 120 * 120
                    feedList.contentY = Math.max(0, Math.min(maxY, feedList.contentY - step))
                    wheel.accepted = true
                    feedList.maybeLoadMore()
                }
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

    ErrorNotifications {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 18
        anchors.bottomMargin: 18
        z: 30
        errorMessage: App.errorMessage
        cardFill: root.panel
        cardBorder: Qt.tint(
            root.paper, Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.48))
        cardAccent: root.danger
        textColor: root.danger
        onDismissed: function(sourceMessage) {
            if (App.errorMessage === sourceMessage)
                App.clearError()
        }
        onCopied: copiedTimer.restart()
    }

    Rectangle {
        id: copiedNotice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        z: 40
        visible: copiedTimer.running
        color: root.panel
        border.color: root.rule
        width: copiedLabel.implicitWidth + 20
        height: copiedLabel.implicitHeight + 10

        Label {
            id: copiedLabel
            anchors.centerIn: parent
            color: root.ink
            font.family: "monospace"
            font.pixelSize: 11
            text: qsTr("Copied to clipboard")
        }

        Timer {
            id: copiedTimer
            interval: 1800
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

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        z: 10
        visible: !App.playerOpen && !root.historyOpen
        color: root.paper
        border.color: root.rule
        width: keybindsLabel.implicitWidth + 20
        height: keybindsLabel.implicitHeight + 12

        Label {
            id: keybindsLabel
            anchors.centerIn: parent
            color: root.mutedInk
            font.pixelSize: 11
            text: keybinds.footerText("feed")
        }
    }

    SimpleSettingsDialog {
        id: settingsDialog
        parent: root.contentItem
    }

    Loader {
        id: historyLoader
        anchors.fill: parent
        z: 15
        active: root.historyOpen
        source: "qrc:/qml/SimpleHistoryPage.qml"

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
