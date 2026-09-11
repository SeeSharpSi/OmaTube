import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import YtClient

Item {
    id: root

    property var player
    property var hostWindow
    signal closeRequested()

    readonly property color accent: App.themeColors.accent
    readonly property color ink: App.themeColors.foreground
    readonly property color mutedInk: App.themeColors.dark_foreground
    readonly property color paper: App.themeColors.background
    readonly property color panel: App.themeColors.lighter_background
    readonly property color rule: App.themeColors.muted
    readonly property color softFill: App.themeColors.selection
    readonly property color danger: App.themeColors.red
    readonly property color chromeInk: "#ffffff"

    readonly property bool isFullscreen: hostWindow
        ? hostWindow.visibility === Window.FullScreen : false
    readonly property bool chromeVisible: !App.pointerWatcher.hidden
    readonly property bool playing: player ? !player.paused : false
    readonly property real durationS: player ? player.duration : 0
    readonly property real positionS: player ? player.position : 0
    readonly property real shownPosition: seekDragging ? seekPreview : positionS
    property bool seekDragging: false
    property real seekPreview: 0
    property int spinnerFrame: 0
    readonly property var spinnerFrames: ["|", "/", "-", "\\"]
    readonly property var qualityOptions: [
        { label: qsTr("Default"), value: -1 },
        { label: qsTr("Auto"), value: 0 },
        { label: qsTr("2160p"), value: 2160 },
        { label: qsTr("1440p"), value: 1440 },
        { label: qsTr("1080p"), value: 1080 },
        { label: qsTr("720p"), value: 720 },
        { label: qsTr("480p"), value: 480 },
        { label: qsTr("360p"), value: 360 }
    ]

    function qualityIndex() {
        for (let i = 0; i < qualityOptions.length; ++i) {
            if (qualityOptions[i].value === App.currentVideoMaximumHeightOverride)
                return i
        }
        return 0
    }

    function qualityLabelForValue(v) {
        for (let i = 0; i < qualityOptions.length; ++i) {
            if (qualityOptions[i].value === v)
                return qualityOptions[i].label
        }
        return v === 0 ? qsTr("Auto") : v + "p"
    }

    // Longest option label, used to size the selector box to the widest
    // dropdown entry instead of a fixed width.
    function widestQualityLabel() {
        let widest = ""
        for (let i = 0; i < qualityOptions.length; ++i) {
            if (qualityOptions[i].label.length > widest.length)
                widest = qualityOptions[i].label
        }
        return widest
    }

    readonly property string overlayMode: player
        ? (player.errorMessage.length > 0 ? "error"
            : player.loading ? "loading"
            : player.ended ? "ended"
            : "none")
        : "none"

    function formatTime(value) {
        if (!isFinite(value) || value < 0)
            value = 0
        const whole = Math.floor(value)
        const hours = Math.floor(whole / 3600)
        const minutes = Math.floor((whole % 3600) / 60)
        const secs = whole % 60
        const m = minutes < 10 ? "0" + minutes : "" + minutes
        const s = secs < 10 ? "0" + secs : "" + secs
        if (hours > 0)
            return hours + ":" + m + ":" + s
        return minutes + ":" + s
    }

    property var hoveredSegment: ({})

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

    function updateSegmentHover(x) {
        if (!App.sponsorBlockEnabled || root.seekDragging || !(root.durationS > 0)) {
            if (root.hoveredSegment.category !== undefined)
                root.hoveredSegment = ({})
            return
        }
        const t = ((x - seekSlider.leftPadding) / Math.max(1, seekSlider.availableWidth))
            * root.durationS
        let found = ({})
        const segments = App.sponsorSegments
        for (let i = 0; i < segments.length; ++i) {
            const segment = segments[i]
            if (segment && t >= segment.start && t <= segment.end) {
                found = segment
                break
            }
        }
        // Only write on category change: reassigning every mouse pixel
        // would relayout the tooltip each move and make it trail the cursor.
        if (root.hoveredSegment.category !== found.category)
            root.hoveredSegment = found
    }

    function toggleFullscreen() {
        if (!hostWindow)
            return
        if (hostWindow.visibility === Window.FullScreen)
            hostWindow.showNormal()
        else
            hostWindow.showFullScreen()
    }

    Timer {
        interval: 120
        repeat: true
        running: player ? player.loading : false
        onTriggered: root.spinnerFrame = (root.spinnerFrame + 1) % root.spinnerFrames.length
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: Qt.rgba(0, 0, 0, 0.72)
        z: 2
        visible: root.chromeVisible

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            ToolButton {
                implicitHeight: 42
                text: qsTr("Back")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: root.closeRequested()

                PointingCursor {}
                background: Item {}

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? "#ffffff" : root.ink
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                id: titleLabel
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                text: App.currentVideoTitle
                color: root.paper
                font.pixelSize: 13
                font.weight: Font.Medium
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                visible: text.length > 0
            }

            Item { Layout.fillWidth: true; visible: titleLabel.visible === false }

            ComboBox {
                id: qualitySelector
                // Hug the widest option label (plus text paddings and a
                // little slack) instead of a fixed width.
                Layout.preferredWidth: Math.ceil(qualityTextMetrics.advanceWidth) + 8 + 24 + 6
                Layout.preferredHeight: 34
                model: root.qualityOptions
                textRole: "label"
                valueRole: "value"
                currentIndex: root.qualityIndex()
                focusPolicy: Qt.NoFocus
                onActivated: App.setCurrentVideoMaximumHeightOverride(Number(currentValue))
                ToolTip.visible: hovered
                ToolTip.text: App.currentVideoMaximumHeightOverride === -1
                    ? qsTr("Quality: Default (%1)").arg(root.qualityLabelForValue(App.currentVideoMaximumHeight))
                    : qsTr("Quality: %1").arg(root.qualityLabelForValue(App.currentVideoMaximumHeightOverride))

                TextMetrics {
                    id: qualityTextMetrics
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    text: root.widestQualityLabel()
                }

                PointingCursor {}
                background: Rectangle {
                    color: qualitySelector.hovered || qualitySelector.popup.visible
                        ? Qt.rgba(1, 1, 1, 0.24) : Qt.rgba(1, 1, 1, 0.14)
                    radius: 4
                    border.width: 1
                    border.color: qualitySelector.hovered || qualitySelector.popup.visible
                        ? Qt.rgba(1, 1, 1, 0.78) : Qt.rgba(1, 1, 1, 0.52)
                }
                contentItem: Text {
                    leftPadding: 8
                    rightPadding: 24
                    text: qualitySelector.displayText
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                indicator: Canvas {
                    x: qualitySelector.width - width - 8
                    y: qualitySelector.height / 2 - height / 2
                    width: 8
                    height: 5
                    readonly property color chevronColor: "#ffffff"
                    onChevronColorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = chevronColor
                        ctx.lineWidth = 1.2
                        ctx.lineCap = "round"
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(0, 0.5)
                        ctx.lineTo(width/2, height-0.5)
                        ctx.lineTo(width, 0.5)
                        ctx.stroke()
                    }
                }
                delegate: ItemDelegate {
                    id: qualityOption
                    required property int index
                    required property var modelData
                    width: qualitySelector.width
                    highlighted: qualitySelector.highlightedIndex === qualityOption.index
                    PointingCursor {}
                    background: Rectangle { color: qualityOption.highlighted ? root.softFill : root.paper }
                    contentItem: RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 6; height: 6
                            color: root.accent
                            visible: qualitySelector.currentIndex === qualityOption.index
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qualityOption.modelData.label
                            color: root.ink
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
                popup: Popup {
                    y: qualitySelector.height + 2
                    width: qualitySelector.width
                    implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
                    padding: 1
                    background: Rectangle { color: root.paper; border.color: root.rule }
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: qualitySelector.popup.visible ? qualitySelector.delegateModel : null
                        currentIndex: qualitySelector.highlightedIndex
                        interactive: false
                    }
                }
            }
        }
    }

    Item {
        id: overlay
        anchors.fill: parent
        z: 1
        visible: root.chromeVisible && root.overlayMode !== "none"

        Rectangle {
            objectName: "videoLoadingFrame"
            anchors.centerIn: parent
            width: height
            height: overlayColumn.implicitHeight + 32
            visible: root.overlayMode === "loading"
            color: Qt.rgba(0.02, 0.02, 0.02, 0.88)
            border.color: root.accent
            border.width: 1
        }

        ColumnLayout {
            id: overlayColumn
            anchors.centerIn: parent
            spacing: 16

            Text {
                objectName: "videoLoadingSpinner"
                Layout.alignment: Qt.AlignHCenter
                text: root.spinnerFrames[root.spinnerFrame]
                visible: root.overlayMode === "loading"
                color: root.chromeInk
                font.pixelSize: 34
                font.family: "monospace"
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                text: root.overlayMode === "error"
                    ? (root.player ? root.player.errorMessage : "")
                    : root.overlayMode === "loading"
                        ? qsTr("Loading...") : qsTr("Playback ended")
                visible: root.overlayMode !== "loading"
                color: root.overlayMode === "error" ? root.danger : root.paper
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Button {
                visible: root.overlayMode === "ended"
                text: qsTr("Replay")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (!root.player)
                        return
                    root.player.seek(0)
                    if (root.player.paused)
                        root.player.togglePaused()
                }

                PointingCursor {}
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 56
        color: Qt.rgba(0, 0, 0, 0.72)
        z: 2
        visible: root.chromeVisible

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            ToolButton {
                id: playButton
                implicitWidth: 44
                implicitHeight: 40
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: if (root.player) root.player.togglePaused()

                PointingCursor {}
                background: Item {}

                contentItem: Canvas {
                    readonly property color color: playButton.hovered ? "#ffffff" : root.ink
                    readonly property bool iconPlaying: root.playing

                    onColorChanged: requestPaint()
                    onIconPlayingChanged: requestPaint()

                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.fillStyle = color
                        if (iconPlaying) {
                            context.fillRect(width * 0.32, height * 0.24, width * 0.13, height * 0.52)
                            context.fillRect(width * 0.57, height * 0.24, width * 0.13, height * 0.52)
                        } else {
                            context.beginPath()
                            context.moveTo(width * 0.30, height * 0.22)
                            context.lineTo(width * 0.30, height * 0.78)
                            context.lineTo(width * 0.76, height * 0.50)
                            context.closePath()
                            context.fill()
                        }
                    }
                }
            }

            ToolButton {
                id: liveButton
                objectName: "liveButton"
                text: qsTr("Live")
                flat: true
                focusPolicy: Qt.NoFocus
                visible: App.currentVideoIsLive
                Accessible.name: qsTr("Go to live")
                Accessible.role: Accessible.Button
                onClicked: if (root.player) root.player.seek(root.durationS)

                PointingCursor {}
                background: Item {}

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? "#ffffff" : root.ink
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                text: root.formatTime(root.shownPosition)
                color: root.ink
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                from: 0
                to: Math.max(1, root.durationS)
                // Knob follows playback unless the user is dragging; the seek is
                // only committed on release so native playback is not thrashed.
                value: root.seekDragging ? root.seekPreview : root.positionS
                onPressedChanged: {
                    if (pressed) {
                        root.seekDragging = true
                        root.seekPreview = value
                    } else {
                        root.seekPreview = value
                        if (root.player)
                            root.player.seek(value)
                        root.seekDragging = false
                        // Re-bind after the drag dropped this binding so the
                        // knob tracks playback again.
                        value = Qt.binding(function() {
                            return root.seekDragging ? root.seekPreview : root.positionS
                        })
                    }
                }
                onMoved: root.seekPreview = value

                PointingCursor {}

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: 4
                    color: root.rule
                    clip: true
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        color: root.accent
                    }
                    Repeater {
                        model: App.sponsorBlockEnabled ? App.sponsorSegments : []
                        delegate: Rectangle {
                            required property var modelData
                            x: (modelData.start / Math.max(1, root.durationS)) * parent.width
                            width: Math.max(2,
                                ((modelData.end - modelData.start) / Math.max(1, root.durationS))
                                * parent.width)
                            height: parent.height
                            color: App.themeColors[App.sponsorColorKey(modelData.category)]
                                ?? App.themeColors.green
                            visible: root.durationS > 0 && modelData.end > modelData.start
                        }
                    }
                }

                MouseArea {
                    id: segmentHover
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: function(mouse) { root.updateSegmentHover(mouse.x) }
                    onExited: root.hoveredSegment = ({})
                }

                Rectangle {
                    id: segmentTip
                    objectName: "segmentTooltip"
                    visible: root.hoveredSegment.category !== undefined
                    // Bind straight to the hover area: routing x through JS
                    // made the tip trail the cursor by a frame.
                    x: Math.min(Math.max(0, segmentHover.mouseX - width / 2),
                        Math.max(0, seekSlider.width - width))
                    y: -height - 8
                    width: tipText.implicitWidth + 16
                    height: tipText.implicitHeight + 10
                    color: root.paper
                    border.color: root.rule
                    border.width: 1
                    Text {
                        id: tipText
                        objectName: "segmentTipText"
                        anchors.centerIn: parent
                        text: root.hoveredSegment.category !== undefined
                            ? root.sponsorLabel(root.hoveredSegment.category) : ""
                        color: root.ink
                        font.pixelSize: 12
                    }
                }
            }

            Text {
                text: root.formatTime(root.durationS)
                color: root.ink
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }

            ToolButton {
                id: muteButton
                text: root.player && root.player.muted ? qsTr("Unmute") : qsTr("Mute")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: if (root.player) root.player.muted = !root.player.muted

                PointingCursor {}
                background: Item {}

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? "#ffffff" : root.ink
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Slider {
                id: volumeSlider
                Layout.preferredWidth: 90
                focusPolicy: Qt.NoFocus
                from: 0
                to: 100
                value: root.player ? root.player.volume : 0
                onMoved: {
                    if (root.player)
                        App.setPlaybackVolume(Math.round(value))
                }
                onPressedChanged: {
                    if (!pressed) {
                        value = Qt.binding(function() {
                            return root.player ? root.player.volume : 0
                        })
                    }
                }

                PointingCursor {}
            }

            ToolButton {
                id: fullscreenButton
                text: root.isFullscreen ? qsTr("Exit") : qsTr("Fullscreen")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: root.toggleFullscreen()

                PointingCursor {}
                background: Item {}

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? "#ffffff" : root.ink
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: player
        onActivated: if (player) player.seek(Math.max(0, (player.position || 0) - 10))
    }

    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: player
        onActivated: if (player) player.seek((player.position || 0) + 10)
    }

    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        enabled: player
        onActivated: {
            if (player) {
                const v = Math.min(100, Math.round((player.volume || 0) + 5))
                App.setPlaybackVolume(v)
            }
        }
    }

    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: player
        onActivated: {
            if (player) {
                const v = Math.max(0, Math.round((player.volume || 0) - 5))
                App.setPlaybackVolume(v)
            }
        }
    }

    Shortcut {
        sequence: "M"
        context: Qt.WindowShortcut
        enabled: player
        onActivated: if (player) player.muted = !player.muted
    }

    Shortcut {
        sequence: "F"
        context: Qt.WindowShortcut
        enabled: hostWindow
        onActivated: root.toggleFullscreen()
    }
}
