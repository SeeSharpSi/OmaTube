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
    objectName: "appWindow"

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
    property bool historyOpen: false
    property bool watchNextOpen: false
    property bool modalOpen: settingsDialog.visible || App.playerOpen
    property int spinnerFrame: 0
    readonly property var spinnerFrames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    readonly property rect spinnerCellBounds: spinnerFontMetrics.tightBoundingRect("⠿")
    readonly property int spinnerVerticalOffset: Math.round(
        Math.ceil(spinnerFontMetrics.height) / 2
        - (spinnerFontMetrics.ascent + spinnerCellBounds.y + spinnerCellBounds.height / 2))

    FontMetrics {
        id: spinnerFontMetrics
        font.family: "monospace"
        font.pixelSize: 16
    }

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

    function showFeedback(message) {
        feedbackLabel.text = message
        feedbackTimer.restart()
    }

    Component.onCompleted: App.startupRefresh()

    Connections {
        target: App
        function onWatchNextFeedback(message) {
            root.showFeedback(message)
        }
    }

    Timer {
        interval: 80
        running: App.refreshing || App.historyLoading
        repeat: true
        onTriggered: root.spinnerFrame = (root.spinnerFrame + 1) % root.spinnerFrames.length
    }

    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: !settingsDialog.visible && !App.playerOpen && !App.refreshing && !App.automationMode
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
        enabled: App.playerOpen || root.historyOpen || root.watchNextOpen
        onActivated: {
            if (root.historyOpen)
                root.historyOpen = false
            else if (root.watchNextOpen)
                root.watchNextOpen = false
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
            root.watchNextOpen = false
        }
    }

    Shortcut {
        sequence: "W"
        context: Qt.WindowShortcut
        enabled: !root.modalOpen && !App.refreshing
        onActivated: {
            if (root.watchNextOpen) {
                root.watchNextOpen = false
                return
            }
            App.reloadWatchNext()
            root.watchNextOpen = true
            root.historyOpen = false
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
            if (root.watchNextOpen) {
                if (watchNextLoader.item)
                    watchNextLoader.item.scrollBy(120)
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
            if (root.watchNextOpen) {
                if (watchNextLoader.item)
                    watchNextLoader.item.scrollBy(-120)
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
        width: Math.min(parent.width - 40, 1120)
        spacing: 14
        visible: !App.playerOpen

        RowLayout {
            Layout.fillWidth: true
            implicitHeight: 42

            Column {
                spacing: 1
                Text { text: "OMA / TUBE"; color: root.ink; font.family: "monospace"; font.pixelSize: 16; font.bold: true }
                Text { text: qsTr("PERSONAL VIDEO LIBRARY"); color: root.mutedInk; font.family: "monospace"; font.pixelSize: 8; font.letterSpacing: 1 }
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "feedNavigationButton"
                Accessible.name: "Show feed"
                Accessible.role: Accessible.Button
                text: qsTr("FEED")
                flat: true
                onClicked: {
                    root.historyOpen = false
                    root.watchNextOpen = false
                }
                contentItem: Text { text: parent.text; color: (!root.historyOpen && !root.watchNextOpen) ? root.panel : parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: (!root.historyOpen && !root.watchNextOpen) ? root.accent : parent.hovered ? root.softFill : "transparent"; border.color: (!root.historyOpen && !root.watchNextOpen) ? root.accent : root.rule }
                PointingCursor {}
            }
            Button {
                objectName: "historyNavigationButton"
                Accessible.name: "Show history"
                Accessible.role: Accessible.Button
                text: qsTr("HISTORY")
                flat: true
                onClicked: {
                    if (!root.historyOpen) {
                        App.reloadWatchHistory()
                        root.historyOpen = true
                        root.watchNextOpen = false
                    }
                }
                contentItem: Text { text: parent.text; color: root.historyOpen ? root.panel : parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: root.historyOpen ? root.accent : parent.hovered ? root.softFill : "transparent"; border.color: root.historyOpen ? root.accent : root.rule }
                PointingCursor {}
            }
            Button {
                objectName: "watchNextNavigationButton"
                Accessible.name: "Show Watch Next"
                Accessible.role: Accessible.Button
                text: qsTr("WATCH NEXT")
                flat: true
                onClicked: {
                    if (!root.watchNextOpen) {
                        App.reloadWatchNext()
                        root.watchNextOpen = true
                        root.historyOpen = false
                    }
                }
                contentItem: Text { text: parent.text; color: root.watchNextOpen ? root.panel : parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: root.watchNextOpen ? root.accent : parent.hovered ? root.softFill : "transparent"; border.color: root.watchNextOpen ? root.accent : root.rule }
                PointingCursor {}
            }
            Button {
                id: settingsButton
                objectName: "settingsNavigationButton"
                Accessible.name: "Open settings"
                Accessible.role: Accessible.Button
                text: qsTr("CONFIG")
                flat: true
                onClicked: settingsDialog.open()
                contentItem: Text { text: parent.text; color: parent.hovered ? root.accent : root.mutedInk; font.family: "monospace"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                background: Rectangle { color: parent.hovered ? root.softFill : "transparent"; border.color: root.rule }
                PointingCursor {}
            }
            Button {
                id: refreshButton
                objectName: "refreshButton"
                Accessible.name: "Refresh feed"
                Accessible.role: Accessible.Button
                implicitWidth: settingsButton.height
                implicitHeight: settingsButton.height
                padding: 0
                enabled: !App.refreshing && !App.automationMode
                flat: true
                onClicked: App.refresh()
                contentItem: Item {
                    Canvas {
                        anchors.centerIn: parent
                        width: 13
                        height: 13
                        visible: !App.refreshing
                        antialiasing: true
                        property color glyphColor: !refreshButton.enabled ? root.mutedInk
                            : refreshButton.hovered ? root.accent : root.ink
                        onGlyphColorChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = glyphColor
                            ctx.fillStyle = glyphColor
                            ctx.lineWidth = 1.5
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            var cx = width / 2
                            var cy = height / 2
                            var r = Math.min(width, height) / 2 - 1.5
                            var start = -0.3 * Math.PI
                            var end = 1.3 * Math.PI
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, start, end, false)
                            ctx.stroke()
                            var px = cx + r * Math.cos(start)
                            var py = cy + r * Math.sin(start)
                            var tip = r * 0.55
                            var halfW = r * 0.38
                            var tx = -Math.sin(start)
                            var ty = Math.cos(start)
                            var nx = Math.cos(start)
                            var ny = Math.sin(start)
                            ctx.beginPath()
                            ctx.moveTo(px + tx * tip, py + ty * tip)
                            ctx.lineTo(px + nx * halfW, py + ny * halfW)
                            ctx.lineTo(px - nx * halfW, py - ny * halfW)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                    Text {
                        visible: App.refreshing
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: root.spinnerVerticalOffset
                        width: parent.width
                        height: parent.height
                        text: root.spinnerFrames[root.spinnerFrame]
                        color: !refreshButton.enabled ? root.mutedInk
                            : refreshButton.hovered ? root.accent : root.ink
                        font.family: "monospace"
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                background: Rectangle {
                    color: refreshButton.hovered ? root.softFill : "transparent"
                    border.color: root.rule
                }
                PointingCursor {}
            }
        }

        ColumnLayout {
            objectName: "feedPage"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.historyOpen && !root.watchNextOpen
            spacing: 14

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
                        objectName: "liveVideo_" + liveDelegate.videoId
                        Accessible.role: Accessible.Button
                        Accessible.name: "Live video " + liveDelegate.videoId + " " + liveDelegate.channelTitle + " " + liveDelegate.videoTitle
                        Accessible.onPressAction: App.openVideo(liveDelegate.videoId)
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
                                    source: App.automationMode ? "" : liveDelegate.avatarUrl
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
                            objectName: "categoryButton_" + categoryButton.categoryId
                            Accessible.name: "Category " + categoryButton.categoryId + " " + categoryButton.name
                            Accessible.role: Accessible.Button
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

            Flickable {
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
                Layout.topMargin: -14
                clip: true
                contentWidth: width
                contentHeight: feedContentColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

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

                Column {
                    id: feedContentColumn
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right

                    GridLayout {
                        id: feedGrid
                        readonly property real cardWidth: Math.max(0,
                            (width - (columns - 1) * columnSpacing) / columns)

                        width: Math.max(0, parent.width - 12)
                        x: Math.max(0, (parent.width - width) / 2)
                        columns: feedList.columnCount
                        columnSpacing: 12
                        rowSpacing: 12

                        Repeater {
                            id: feedRepeater
                            model: App.feed

                            delegate: Item {
                                id: feedDelegate
                                objectName: "feedVideo_" + feedDelegate.videoId
                                Accessible.role: Accessible.Button
                                Accessible.name: "Video " + feedDelegate.videoId + " " + feedDelegate.title
                                Accessible.onPressAction: App.openVideo(feedDelegate.videoId)
                                required property string videoId
                                required property string channelTitle
                                required property string title
                                required property date publishedAt
                                required property int watchProgressPercent

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredWidth: feedGrid.cardWidth
                                implicitHeight: thumbnail.height + 10 + infoColumn.implicitHeight + 10
                                    + (feedDelegate.watchProgressPercent >= 0
                                       ? 5 + watchProgress.implicitHeight : 0)

                                Rectangle {
                                    anchors.fill: parent
                                    color: root.glassPanel

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
                                        source: App.automationMode ? "" : "https://i.ytimg.com/vi/" + feedDelegate.videoId + "/hqdefault.jpg"
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
                                        id: infoColumn
                                        anchors.top: thumbnail.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.topMargin: 10
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 5

                                        Text { width: parent.width; text: feedDelegate.title; color: root.ink; font.family: "monospace"; font.pixelSize: 14; font.weight: Font.Medium; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }

                                        Text { width: parent.width; text: feedDelegate.channelTitle + "  \u00b7  " + root.relativeTime(feedDelegate.publishedAt); color: root.mutedInk; font.family: "monospace"; font.pixelSize: 10; elide: Text.ElideRight }
                                    }

                                    Text {
                                        id: watchProgress
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        anchors.bottomMargin: 10
                                        text: qsTr("%1% watched").arg(feedDelegate.watchProgressPercent)
                                        color: root.neonYellow
                                        font.family: "monospace"
                                        font.pixelSize: 10
                                        visible: feedDelegate.watchProgressPercent >= 0
                                    }

                                    Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; height: 3; color: root.neonYellow; visible: feedDelegate.watchProgressPercent >= 0; width: parent.width * Math.max(0, Math.min(100, feedDelegate.watchProgressPercent)) / 100 }

                                    Rectangle {
                                        objectName: "feedVideoOutline_" + feedDelegate.videoId
                                        anchors.fill: parent
                                        z: 1
                                        color: "transparent"
                                        border.color: feedHover.hovered ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 1.0) : root.rule
                                        border.width: feedHover.hovered ? 2 : 1
                                    }
                                }

                                HoverHandler {
                                    id: feedHover
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler { onTapped: App.openVideo(feedDelegate.videoId) }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: App.addToWatchNext(feedDelegate.videoId)
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: visible ? 52 : 0
                        visible: App.historyLoading || App.historyHasMore

                        Label {
                            anchors.centerIn: parent
                            color: root.mutedInk
                            font.pixelSize: 12
                            text: App.historyLoading
                                  ? root.spinnerFrames[root.spinnerFrame]
                                  : (feedRepeater.count > 0 ? qsTr("Load more") : "")
                        }

                        TapHandler {
                            enabled: !App.historyLoading && App.historyHasMore
                            cursorShape: Qt.PointingHandCursor
                            onTapped: App.loadMoreHistory()
                        }
                    }

                    Item { width: 1; height: 18 }
                }

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 430)
                    spacing: 8
                    visible: feedRepeater.count === 0 && !App.refreshing

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
                        text: qsTr("Open Config to add a channel, then refresh.")
                    }
                }
            }
        }

        Loader {
            id: historyLoader
            objectName: "historyLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.historyOpen
            visible: root.historyOpen
            source: "qrc:/qml/HistoryPage.qml"

            onLoaded: {
                item.videoSelected.connect(function(videoId) {
                    root.historyOpen = false
                    App.openVideo(videoId)
                })
            }
        }

        Loader {
            id: watchNextLoader
            objectName: "watchNextLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.watchNextOpen
            visible: root.watchNextOpen
            source: "qrc:/qml/WatchNextPage.qml"

            onLoaded: {
                item.videoSelected.connect(function(videoId) {
                    root.watchNextOpen = false
                    App.openVideo(videoId)
                })
            }
        }
    }

    ErrorNotifications {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 18
        anchors.bottomMargin: refreshPill.visible ? 52 : 18
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
        onCopied: root.showFeedback(qsTr("Copied to clipboard"))
    }

    Rectangle {
        id: feedbackNotice
        objectName: "feedbackNotice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        z: 40
        visible: feedbackTimer.running
        color: root.panel
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 1.0)
        width: feedbackLabel.implicitWidth + 20
        height: feedbackLabel.implicitHeight + 10

        Label {
            id: feedbackLabel
            objectName: "feedbackLabel"
            anchors.centerIn: parent
            color: root.ink
            font.family: "monospace"
            font.pixelSize: 11
            text: qsTr("Copied to clipboard")
        }

        Timer {
            id: feedbackTimer
            interval: 1800
        }
    }

    Rectangle {
        id: refreshPill
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 18
        anchors.bottomMargin: 12
        z: 10
        width: refreshRow.implicitWidth + 16
        height: 32
        visible: App.refreshing && !App.playerOpen
        color: root.panel
        border.color: root.rule

        Row {
            id: refreshRow
            anchors.centerIn: parent
            spacing: 7

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.spinnerVerticalOffset
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
        visible: !App.playerOpen
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
            text: keybinds.footerText(root.historyOpen ? "history" : root.watchNextOpen ? "watchnext" : "feed").split("\n").join("  /  ")
        }
    }

    SettingsDialog {
        id: settingsDialog
        transientParent: root
    }

    Loader {
        id: playerLoader
        objectName: "playerLoader"
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
