pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import YtClient

ApplicationWindow {
    id: root

    readonly property var themeColors: App.themeColors
    readonly property var themeIds: ["default", "rose-pine", "nord", "omarchy"]
    readonly property color accent: themeColors.accent
    readonly property color ink: themeColors.foreground
    readonly property color mutedInk: themeColors.dark_foreground
    readonly property color paper: Qt.rgba(themeColors.background.r,
        themeColors.background.g, themeColors.background.b, 0.78)
    readonly property color popupColor: Qt.rgba(themeColors.background.r,
        themeColors.background.g, themeColors.background.b, 1)
    readonly property color panel: Qt.rgba(themeColors.lighter_background.r,
        themeColors.lighter_background.g, themeColors.lighter_background.b, 0.92)
    readonly property color rule: Qt.tint(themeColors.muted,
        Qt.rgba(themeColors.accent.r, themeColors.accent.g, themeColors.accent.b, 0.24))
    readonly property color softFill: Qt.tint(themeColors.selection,
        Qt.rgba(themeColors.accent.r, themeColors.accent.g, themeColors.accent.b, 0.32))
    readonly property color danger: themeColors.red
    readonly property color errorFill: Qt.tint(
        paper, Qt.rgba(danger.r, danger.g, danger.b, 0.14))
    readonly property string chromeFont: "monospace"
    property var selectedCategoryIds: []
    property string pendingAction: ""
    property var pendingId: -1
    property string pendingName: ""
    readonly property var playbackBackends: App.mpvAvailable
        ? [ { label: qsTr("Official embedded player"), value: "iframe" },
            { label: qsTr("Embedded mpv"), value: "mpv" } ]
        : [ { label: qsTr("Official embedded player"), value: "iframe" } ]
    readonly property var playbackQualities: [
        { label: qsTr("Auto"), value: 0 },
        { label: qsTr("2160p"), value: 2160 },
        { label: qsTr("1440p"), value: 1440 },
        { label: qsTr("1080p"), value: 1080 },
        { label: qsTr("720p"), value: 720 },
        { label: qsTr("480p"), value: 480 },
        { label: qsTr("360p"), value: 360 }
    ]

    function playbackBackendIndex() {
        for (let i = 0; i < playbackBackends.length; ++i) {
            if (playbackBackends[i].value === App.videoBackend)
                return i
        }
        return 0
    }

    function playbackQualityIndex() {
        for (let i = 0; i < playbackQualities.length; ++i) {
            if (playbackQualities[i].value === App.maximumVideoHeight)
                return i
        }
        return 0
    }

    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.WindowModal
    width: 900
    height: 720
    minimumWidth: 700
    minimumHeight: 500
    visible: false
    title: qsTr("Settings")
    color: root.panel
    palette.window: root.panel
    palette.windowText: root.ink
    palette.base: root.paper
    palette.alternateBase: root.softFill
    palette.text: root.ink
    palette.button: root.softFill
    palette.buttonText: root.ink
    palette.highlight: root.accent
    palette.highlightedText: root.panel
    palette.mid: root.rule
    palette.placeholderText: root.mutedInk

    function open() {
        selectedCategoryIds = App.selectedCategoryId >= 0 ? [App.selectedCategoryId] : []
        keyInput.clear()
        rememberKey.checked = false
        if (!visible && transientParent) {
            x = transientParent.x + Math.round((transientParent.width - width) / 2)
            y = transientParent.y + Math.round((transientParent.height - height) / 2)
        }
        visible = true
        raise()
        requestActivate()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: root.close()
    }

    function confirmDelete(action, id, name) {
        pendingAction = action
        pendingId = id
        pendingName = name
        confirmDialog.open()
    }

    Connections {
        target: App

        function onChannelAdded(title) {
            channelInput.clear()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.panel
        border.color: root.rule
        border.width: 1
    }

    FramelessResizeHelper { targetWindow: root }

    Rectangle {
        id: settingsHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: 62
        height: implicitHeight
        color: root.panel

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.rule
        }

        MouseArea {
            anchors.fill: parent
            z: 0
            onPressed: root.startSystemMove()
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("OMA / SETTINGS")
            color: root.ink
            font.family: root.chromeFont
            font.pixelSize: 17
            font.letterSpacing: 1.4
            font.weight: Font.DemiBold
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 25
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            text: qsTr("CONFIGURATION")
            color: root.mutedInk
            font.family: root.chromeFont
            font.pixelSize: 9
            font.letterSpacing: 1.2
        }

        ToolButton {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 38
            height: 38
            text: "\u00d7"
            flat: true
            onClicked: root.close()

            PointingCursor {}

            background: Rectangle {
                color: parent.hovered ? root.softFill : "transparent"
                border.color: parent.hovered ? root.accent : root.rule
                border.width: 1
            }

            contentItem: Canvas {
                property color lineColor: parent.hovered ? root.ink : root.mutedInk

                onLineColorChanged: requestPaint()
                onPaint: {
                    const context = getContext("2d")
                    const centerX = width / 2
                    const centerY = height / 2
                    context.reset()
                    context.strokeStyle = lineColor
                    context.lineWidth = 1.3
                    context.lineCap = "round"
                    context.beginPath()
                    context.moveTo(centerX - 5, centerY - 5)
                    context.lineTo(centerX + 5, centerY + 5)
                    context.moveTo(centerX + 5, centerY - 5)
                    context.lineTo(centerX - 5, centerY + 5)
                    context.stroke()
                }
            }
        }
    }

    ColumnLayout {
        id: settingsContent
        anchors.top: settingsHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 0

            background: Rectangle {
                color: Qt.tint(root.paper, Qt.rgba(
                    root.accent.r, root.accent.g, root.accent.b, 0.035))

                Rectangle {
                    anchors.bottom: parent.bottom
                    x: tabs.currentIndex * width
                    width: parent.width / tabs.count
                    height: 2
                    color: Qt.tint(root.accent, Qt.rgba(
                        root.accent.r, root.accent.g, root.accent.b, 0.82))
                }
            }

            TabButton {
                id: channelsTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Channels")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: channelsTab.text
                    color: channelsTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: channelsTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: categoriesTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Categories")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: categoriesTab.text
                    color: categoriesTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: categoriesTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: feedTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Feed")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: feedTab.text
                    color: feedTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: feedTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: appearanceTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Appearance")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: appearanceTab.text
                    color: appearanceTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: appearanceTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: apiTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Data API")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: apiTab.text
                    color: apiTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: apiTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: playbackTab
                height: tabs.height
                implicitHeight: tabs.height
                width: tabs.width / tabs.count
                text: qsTr("Playback")

                PointingCursor {}

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: playbackTab.text
                    color: playbackTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.family: root.chromeFont
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                    font.weight: playbackTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        text: qsTr("Add a channel")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: channelInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("Handle with or without @, channel URL, or UC channel ID")
                            Layout.preferredHeight: 40
                            enabled: !App.addingChannel
                            selectByMouse: true
                            onAccepted: addChannelButton.clicked()

                            background: Rectangle {
                                color: root.paper
                                border.color: channelInput.activeFocus ? root.accent : root.rule
                                border.width: 1
                            }
                        }

                        Button {
                            id: addChannelButton
                            text: App.addingChannel ? qsTr("Adding...") : qsTr("Add")
                            enabled: channelInput.text.trim().length > 0 && !App.addingChannel
                            onClicked: App.addChannel(channelInput.text, root.selectedCategoryIds)
                            Layout.preferredHeight: 40

                            PointingCursor {}
                        }
                    }

                    Label {
                        text: qsTr("Add to categories")
                        color: root.mutedInk
                        font.pixelSize: 12
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 6

                        Repeater {
                            id: addCategoryRepeater
                            model: App.categories

                            delegate: SquareCheckBox {
                                id: addCategoryCheck
                                required property var categoryId
                                required property string name

                                text: addCategoryCheck.name
                                checked: root.selectedCategoryIds.indexOf(addCategoryCheck.categoryId) !== -1

                                onToggled: {
                                    let ids = root.selectedCategoryIds.slice()
                                    const position = ids.indexOf(addCategoryCheck.categoryId)
                                    if (addCategoryCheck.checked && position === -1)
                                        ids.push(addCategoryCheck.categoryId)
                                    else if (!addCategoryCheck.checked && position !== -1)
                                        ids.splice(position, 1)
                                    root.selectedCategoryIds = ids
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: addCategoryRepeater.count === 0
                        text: qsTr("No categories yet. Add one from the Categories tab.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: root.rule
                    }

                    Label {
                        text: qsTr("Subscribed channels")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: App.channels
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            id: channelDelegate
                            required property string channelId
                            required property string title
                            required property string handle
                            required property var categoryIds

                            width: ListView.view.width
                            height: channelContent.implicitHeight + 22
                            color: root.paper
                            border.color: root.rule

                            ColumnLayout {
                                id: channelContent
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 5

                                RowLayout {
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            text: channelDelegate.title
                                            color: root.ink
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: channelDelegate.handle
                                            color: root.mutedInk
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Button {
                                        text: qsTr("Remove")
                                        flat: true
                                        onClicked: root.confirmDelete(
                                            "channel", channelDelegate.channelId, channelDelegate.title)

                                        PointingCursor {}
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: childrenRect.height
                                    spacing: 4

                                    Repeater {
                                        model: App.categories

                                        delegate: SquareCheckBox {
                                            id: membershipCheck
                                            required property var categoryId
                                            required property string name

                                            text: membershipCheck.name
                                            checked: channelDelegate.categoryIds.indexOf(
                                                membershipCheck.categoryId) !== -1

                                            onToggled: App.setChannelInCategory(
                                                channelDelegate.channelId,
                                                membershipCheck.categoryId,
                                                membershipCheck.checked)
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("No channels added yet.")
                            color: root.mutedInk
                            visible: parent.count === 0
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Import JSON")
                            onClicked: channelImportDialog.open()

                            PointingCursor {}
                        }

                        Button {
                            text: qsTr("Export JSON")
                            onClicked: channelExportDialog.open()

                            PointingCursor {}
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Create categories for switching between focused feeds. Channels can belong to more than one.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Import JSON")
                            onClicked: categoryImportDialog.open()

                            PointingCursor {}
                        }

                        Button {
                            text: qsTr("Export JSON")
                            onClicked: categoryExportDialog.open()

                            PointingCursor {}
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: newCategoryInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("New category")
                            Layout.preferredHeight: 40
                            selectByMouse: true
                            onAccepted: {
                                if (App.addCategory(text))
                                    clear()
                            }

                            background: Rectangle {
                                color: root.paper
                                border.color: newCategoryInput.activeFocus ? root.accent : root.rule
                                border.width: 1
                            }
                        }

                        Button {
                            text: qsTr("Add")
                            enabled: newCategoryInput.text.trim().length > 0
                            Layout.preferredHeight: 40

                            PointingCursor {}

                            onClicked: {
                                if (App.addCategory(newCategoryInput.text))
                                    newCategoryInput.clear()
                            }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: App.categories
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            id: categoryDelegate
                            required property var categoryId
                            required property string name

                            width: ListView.view.width
                            height: 54
                            color: root.paper
                            border.color: root.rule

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                TextField {
                                    id: categoryName
                                    Layout.fillWidth: true
                                    text: categoryDelegate.name
                                    selectByMouse: true

                                    background: Rectangle {
                                        color: root.paper
                                        border.color: categoryName.activeFocus ? root.accent : root.rule
                                        border.width: 1
                                    }
                                }

                                Button {
                                    text: qsTr("Save")
                                    enabled: categoryName.text.trim().length > 0
                                             && categoryName.text.trim() !== categoryDelegate.name

                                    PointingCursor {}

                                    onClicked: App.renameCategory(
                                        categoryDelegate.categoryId, categoryName.text)
                                }

                                Button {
                                    text: qsTr("Delete")
                                    flat: true
                                    onClicked: root.confirmDelete(
                                        "category", categoryDelegate.categoryId, categoryDelegate.name)

                                    PointingCursor {}
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("No categories yet.")
                            color: root.mutedInk
                            visible: parent.count === 0
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        text: qsTr("Short video filter")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Hide videos at or below a chosen duration. YouTube does not expose a Shorts flag, so duration is used instead.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 148
                        color: root.popupColor
                        border.color: root.rule

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10

                            Label {
                                text: qsTr("Cutoff")
                                color: root.ink
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Label { text: qsTr("Hide videos up to"); color: root.ink }

                                SpinBox {
                                    id: shortVideoCutoff
                                    Layout.preferredWidth: 100
                                    from: 0
                                    to: 60
                                    value: App.shortVideoCutoffMinutes
                                    editable: true
                                    onValueModified: App.setShortVideoCutoffMinutes(value)
                                    PointingCursor {}
                                }

                                Label { text: qsTr("minutes"); color: root.ink }
                                Item { Layout.fillWidth: true }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: shortVideoCutoff.value === 0
                                    ? qsTr("Duration filtering is off.")
                                    : qsTr("Videos %1 minutes or shorter are hidden from every feed.")
                                          .arg(shortVideoCutoff.value)
                                color: root.mutedInk
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        text: qsTr("Theme")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Choose the colors used throughout OmaTube.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    ComboBox {
                        id: themeSelector
                        Layout.preferredWidth: 260
                        implicitHeight: 40
                        model: [
                            qsTr("Default"),
                            qsTr("Rose Pine"),
                            qsTr("Nord"),
                            qsTr("Omarchy")
                        ]
                        currentIndex: Math.max(0, root.themeIds.indexOf(App.themeId))
                        onActivated: App.setThemeId(root.themeIds[currentIndex])

                        PointingCursor {}

                        background: Rectangle {
                            color: themeSelector.hovered || themeSelector.popup.visible
                                ? root.softFill : root.paper
                            border.color: themeSelector.hovered || themeSelector.popup.visible
                                ? root.mutedInk : root.rule
                        }

                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 34
                            text: themeSelector.displayText
                            color: root.ink
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Canvas {
                            x: themeSelector.width - width - 12
                            y: themeSelector.height / 2 - height / 2
                            width: 10
                            height: 6

                            readonly property color chevronColor:
                                themeSelector.hovered || themeSelector.popup.visible
                                    ? root.ink : root.mutedInk

                            onChevronColorChanged: requestPaint()

                            onPaint: {
                                const context = getContext("2d")
                                context.reset()
                                context.strokeStyle = chevronColor
                                context.lineWidth = 1.4
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                context.beginPath()
                                context.moveTo(0, 0.5)
                                context.lineTo(width / 2, height - 0.5)
                                context.lineTo(width, 0.5)
                                context.stroke()
                            }
                        }

                        delegate: ItemDelegate {
                            id: themeOption
                            required property int index
                            required property string modelData

                            width: themeSelector.width
                            highlighted: themeSelector.highlightedIndex === themeOption.index

                            PointingCursor {}

                            background: Rectangle {
                                color: themeOption.highlighted ? root.softFill : root.popupColor
                            }

                            contentItem: RowLayout {
                                spacing: 10

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 8
                                    height: 8
                                    visible: themeSelector.currentIndex === themeOption.index
                                    color: root.accent
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: themeOption.modelData
                                    color: themeSelector.currentIndex === themeOption.index
                                        ? root.ink : root.mutedInk
                                    font.pixelSize: 13
                                    font.weight: themeSelector.currentIndex === themeOption.index
                                        ? Font.DemiBold : Font.Normal
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        popup: Popup {
                            y: themeSelector.height + 2
                            width: themeSelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
                            padding: 1

                            background: Rectangle {
                                color: root.popupColor
                                border.color: root.rule
                            }

                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: themeSelector.popup.visible
                                    ? themeSelector.delegateModel : null
                                currentIndex: themeSelector.highlightedIndex
                                interactive: false
                                spacing: 0
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: App.themeId === "omarchy"
                        text: App.omarchyThemeAvailable
                            ? (App.omarchyThemeName.length > 0
                                ? qsTr("Following Omarchy system theme: %1")
                                      .arg(App.omarchyThemeName)
                                : qsTr("Following the current Omarchy system theme."))
                            : qsTr("No Omarchy system theme was found. Nord is being used instead.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: root.rule
                    }

                    Label {
                        text: qsTr("Interface")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    SquareCheckBox {
                        id: simpleUiCheck
                        text: qsTr("Use simple UI")
                        checked: App.simpleUi

                        onToggled: {
                            if (checked !== App.simpleUi)
                                App.setSimpleUi(checked)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Use the original compact list layout and opaque controls.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        Layout.fillWidth: true
                        text: App.apiKeyConfigured
                            ? qsTr("An API key is configured. Enter a new key to replace it.")
                            : qsTr("Optional: enter a YouTube Data API v3 key for the official metadata backend. Without one, OmaTube uses public feeds and yt-dlp when available.")
                        color: root.ink
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }

                    TextField {
                        id: keyInput
                        Layout.fillWidth: true
                        placeholderText: qsTr("YouTube Data API key")
                        echoMode: TextInput.Password
                        selectByMouse: true
                        onAccepted: saveKeyButton.clicked()

                        background: Rectangle {
                            color: root.paper
                            border.color: keyInput.activeFocus ? root.accent : root.rule
                            border.width: 1
                        }
                    }

                    SquareCheckBox {
                        id: rememberKey
                        text: qsTr("Remember in local settings")
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Remembering stores the key as plain local configuration. Leave this off or use YT_CLIENT_API_KEY to avoid local storage.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Button {
                            text: qsTr("Clear current key")
                            visible: App.apiKeyConfigured
                            onClicked: App.clearApiKey()

                            PointingCursor {}

                            contentItem: Text {
                                text: parent.text
                                color: root.danger
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: parent.hovered ? root.errorFill : root.popupColor
                                border.color: root.danger
                                border.width: 1
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            id: saveKeyButton
                            text: qsTr("Use key")
                            enabled: keyInput.text.trim().length > 0

                            PointingCursor {}

                            onClicked: {
                                if (App.setApiKey(keyInput.text, rememberKey.checked))
                                    keyInput.clear()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: root.rule
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("After adding or changing a key, refresh the feed from the main window.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Label {
                        text: qsTr("Playback")
                        color: root.ink
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Choose how videos are played. The official player uses YouTube's embedded viewer; mpv offers native playback when available.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: qsTr("Backend")
                        color: root.ink
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    ComboBox {
                        id: backendSelector
                        Layout.fillWidth: true
                        implicitHeight: 40
                        model: root.playbackBackends
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.playbackBackendIndex()
                        onActivated: App.setVideoBackend(String(currentValue))

                        PointingCursor {}

                        background: Rectangle {
                            color: backendSelector.hovered || backendSelector.popup.visible
                                ? root.softFill : root.paper
                            border.color: backendSelector.hovered || backendSelector.popup.visible
                                ? root.mutedInk : root.rule
                        }

                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 34
                            text: backendSelector.displayText
                            color: root.ink
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Canvas {
                            x: backendSelector.width - width - 12
                            y: backendSelector.height / 2 - height / 2
                            width: 10
                            height: 6

                            readonly property color chevronColor:
                                backendSelector.hovered || backendSelector.popup.visible
                                    ? root.ink : root.mutedInk

                            onChevronColorChanged: requestPaint()

                            onPaint: {
                                const context = getContext("2d")
                                context.reset()
                                context.strokeStyle = chevronColor
                                context.lineWidth = 1.4
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                context.beginPath()
                                context.moveTo(0, 0.5)
                                context.lineTo(width / 2, height - 0.5)
                                context.lineTo(width, 0.5)
                                context.stroke()
                            }
                        }

                        delegate: ItemDelegate {
                            id: backendOption
                            required property int index
                            required property var modelData

                            width: backendSelector.width
                            highlighted: backendSelector.highlightedIndex === backendOption.index

                            PointingCursor {}

                            background: Rectangle {
                                color: backendOption.highlighted ? root.softFill : root.popupColor
                            }

                            contentItem: RowLayout {
                                spacing: 10

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 8
                                    height: 8
                                    visible: backendSelector.currentValue === backendOption.modelData.value
                                    color: root.accent
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: backendOption.modelData.label
                                    color: backendSelector.currentValue === backendOption.modelData.value
                                        ? root.ink : root.mutedInk
                                    font.pixelSize: 13
                                    font.weight: backendSelector.currentValue === backendOption.modelData.value
                                        ? Font.DemiBold : Font.Normal
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        popup: Popup {
                            y: backendSelector.height + 2
                            width: backendSelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
                            padding: 1

                            background: Rectangle {
                                color: root.popupColor
                                border.color: root.rule
                            }

                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: backendSelector.popup.visible
                                    ? backendSelector.delegateModel : null
                                currentIndex: backendSelector.highlightedIndex
                                interactive: false
                                spacing: 0
                            }
                        }
                    }

                    Label {
                        text: qsTr("Preferred maximum quality")
                        color: root.ink
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    ComboBox {
                        id: qualitySelector
                        Layout.fillWidth: true
                        implicitHeight: 40
                        model: root.playbackQualities
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.playbackQualityIndex()
                        enabled: App.mpvAvailable && backendSelector.currentValue === "mpv"
                        onActivated: App.setMaximumVideoHeight(Number(currentValue))

                        PointingCursor {}

                        background: Rectangle {
                            color: (qualitySelector.hovered || qualitySelector.popup.visible)
                                   && qualitySelector.enabled
                                ? root.softFill : root.paper
                            border.color: (qualitySelector.hovered || qualitySelector.popup.visible)
                                   && qualitySelector.enabled
                                ? root.mutedInk : root.rule
                        }

                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 34
                            text: qualitySelector.displayText
                            color: qualitySelector.enabled ? root.ink : root.mutedInk
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Canvas {
                            x: qualitySelector.width - width - 12
                            y: qualitySelector.height / 2 - height / 2
                            width: 10
                            height: 6

                            readonly property color chevronColor:
                                qualitySelector.enabled
                                && (qualitySelector.hovered || qualitySelector.popup.visible)
                                    ? root.ink : root.mutedInk

                            onChevronColorChanged: requestPaint()

                            onPaint: {
                                const context = getContext("2d")
                                context.reset()
                                context.strokeStyle = chevronColor
                                context.lineWidth = 1.4
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                context.beginPath()
                                context.moveTo(0, 0.5)
                                context.lineTo(width / 2, height - 0.5)
                                context.lineTo(width, 0.5)
                                context.stroke()
                            }
                        }

                        delegate: ItemDelegate {
                            id: qualityOption
                            required property int index
                            required property var modelData

                            width: qualitySelector.width
                            highlighted: qualitySelector.highlightedIndex === qualityOption.index

                            PointingCursor {}

                            background: Rectangle {
                                color: qualityOption.highlighted ? root.softFill : root.popupColor
                            }

                            contentItem: RowLayout {
                                spacing: 10

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 8
                                    height: 8
                                    visible: qualitySelector.currentValue === qualityOption.modelData.value
                                    color: root.accent
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: qualityOption.modelData.label
                                    color: qualitySelector.currentValue === qualityOption.modelData.value
                                        ? root.ink : root.mutedInk
                                    font.pixelSize: 13
                                    font.weight: qualitySelector.currentValue === qualityOption.modelData.value
                                        ? Font.DemiBold : Font.Normal
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        popup: Popup {
                            y: qualitySelector.height + 2
                            width: qualitySelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
                            padding: 1

                            background: Rectangle {
                                color: root.popupColor
                                border.color: root.rule
                            }

                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: qualitySelector.popup.visible
                                    ? qualitySelector.delegateModel : null
                                currentIndex: qualitySelector.highlightedIndex
                                interactive: false
                                spacing: 0
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Quality is a preferred maximum; the actual resolution may be lower. The official player controls quality automatically.")
                        color: root.mutedInk
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: settingsError.implicitHeight + 18
            color: root.errorFill
            border.color: Qt.tint(root.danger, Qt.rgba(
                root.danger.r, root.danger.g, root.danger.b, 0.42))
            border.width: 1
            visible: App.errorMessage.length > 0

            Label {
                id: settingsError
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: errorClose.left
                anchors.margins: 9
                anchors.rightMargin: 33
                text: App.errorMessage
                color: root.danger
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Rectangle {
                id: errorClose
                anchors.top: parent.top
                anchors.right: parent.right
                width: 24
                height: 24
                color: root.errorFill
                border.color: root.danger

                Text {
                    anchors.centerIn: parent
                    text: "x"
                    color: root.danger
                    font.family: root.chromeFont
                    font.pixelSize: 16
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.clearError()
                }
            }
        }
    }

    Dialog {
        id: confirmDialog
        parent: root.contentItem
        anchors.centerIn: parent
        modal: true
        title: root.pendingAction === "category"
            ? qsTr("Delete category?")
            : qsTr("Remove channel?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            width: 320
            wrapMode: Text.Wrap
            text: root.pendingAction === "category"
                ? qsTr("Delete '%1'? Channels and cached videos will be kept.").arg(root.pendingName)
                : qsTr("Remove '%1' and its cached videos?").arg(root.pendingName)
        }

        onAccepted: {
            if (root.pendingAction === "category")
                App.removeCategory(root.pendingId)
            else
                App.removeChannel(root.pendingId)
        }
    }

    FileDialog {
        id: channelImportDialog
        title: qsTr("Import channels")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: App.importChannels(selectedFile)
    }

    FileDialog {
        id: channelExportDialog
        title: qsTr("Export channels")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: App.exportChannels(selectedFile)
    }

    FileDialog {
        id: categoryImportDialog
        title: qsTr("Import categories")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: App.importCategories(selectedFile)
    }

    FileDialog {
        id: categoryExportDialog
        title: qsTr("Export categories")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: App.exportCategories(selectedFile)
    }
}
