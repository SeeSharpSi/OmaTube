pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Dialog {
    id: root

    readonly property color ink: "#24221e"
    readonly property color mutedInk: "#716d65"
    readonly property color paper: "#f7f4ed"
    readonly property color panel: "#fffdf8"
    readonly property color rule: "#ded8cc"
    readonly property color softFill: "#eee9df"
    property var selectedCategoryIds: []
    property string pendingAction: ""
    property var pendingId: -1
    property string pendingName: ""

    modal: true
    width: Math.min(parent ? parent.width - 32 : 760, 760)
    height: Math.min(parent ? parent.height - 32 : 660, 660)
    anchors.centerIn: parent
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function confirmDelete(action, id, name) {
        pendingAction = action
        pendingId = id
        pendingName = name
        confirmDialog.open()
    }

    onOpened: {
        selectedCategoryIds = App.selectedCategoryId >= 0 ? [App.selectedCategoryId] : []
        keyInput.clear()
        rememberKey.checked = false
    }

    Connections {
        target: App

        function onChannelAdded(title) {
            channelInput.clear()
        }
    }

    Overlay.modal: Rectangle { color: "#76000000" }

    background: Rectangle {
        color: root.panel
        border.color: root.rule
        border.width: 1
    }

    header: Rectangle {
        implicitHeight: 64
        color: root.panel

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.rule
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Settings")
            color: root.ink
            font.pixelSize: 21
            font.weight: Font.DemiBold
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

            background: Item {}

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

    contentItem: ColumnLayout {
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            background: Rectangle { color: root.paper }

            TabButton {
                id: channelsTab
                text: qsTr("Channels")

                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: channelsTab.checked ? root.ink : "transparent"
                    }
                }

                contentItem: Text {
                    text: channelsTab.text
                    color: channelsTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.weight: channelsTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: categoriesTab
                text: qsTr("Categories")

                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: categoriesTab.checked ? root.ink : "transparent"
                    }
                }

                contentItem: Text {
                    text: categoriesTab.text
                    color: categoriesTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.weight: categoriesTab.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TabButton {
                id: apiTab
                text: qsTr("API key")

                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: apiTab.checked ? root.ink : "transparent"
                    }
                }

                contentItem: Text {
                    text: apiTab.text
                    color: apiTab.checked ? root.ink : root.mutedInk
                    font.pixelSize: 13
                    font.weight: apiTab.checked ? Font.DemiBold : Font.Normal
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
                            placeholderText: qsTr("@handle, channel URL, or UC channel ID")
                            enabled: !App.addingChannel
                            selectByMouse: true
                            onAccepted: addChannelButton.clicked()
                        }

                        Button {
                            id: addChannelButton
                            text: App.addingChannel ? qsTr("Adding...") : qsTr("Add")
                            enabled: channelInput.text.trim().length > 0 && !App.addingChannel
                            onClicked: App.addChannel(channelInput.text, root.selectedCategoryIds)
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

                            delegate: CheckBox {
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
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: childrenRect.height
                                    spacing: 4

                                    Repeater {
                                        model: App.categories

                                        delegate: CheckBox {
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

                        TextField {
                            id: newCategoryInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("New category")
                            selectByMouse: true
                            onAccepted: {
                                if (App.addCategory(text))
                                    clear()
                            }
                        }

                        Button {
                            text: qsTr("Add")
                            enabled: newCategoryInput.text.trim().length > 0
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
                                }

                                Button {
                                    text: qsTr("Save")
                                    enabled: categoryName.text.trim().length > 0
                                             && categoryName.text.trim() !== categoryDelegate.name
                                    onClicked: App.renameCategory(
                                        categoryDelegate.categoryId, categoryName.text)
                                }

                                Button {
                                    text: qsTr("Delete")
                                    flat: true
                                    onClicked: root.confirmDelete(
                                        "category", categoryDelegate.categoryId, categoryDelegate.name)
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
                        Layout.fillWidth: true
                        text: App.apiKeyConfigured
                            ? qsTr("An API key is configured. Enter a new key to replace it.")
                            : qsTr("Enter your YouTube Data API v3 key. This is not a YouTube login.")
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
                    }

                    CheckBox {
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
                            flat: true
                            onClicked: App.clearApiKey()
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            id: saveKeyButton
                            text: qsTr("Use key")
                            enabled: keyInput.text.trim().length > 0
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
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: settingsError.implicitHeight + 18
            color: "#f6dddd"
            visible: App.errorMessage.length > 0

            Label {
                id: settingsError
                anchors.fill: parent
                anchors.margins: 9
                text: App.errorMessage
                color: "#712222"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            TapHandler { onTapped: App.clearError() }
        }
    }

    Dialog {
        id: confirmDialog
        parent: Overlay.overlay
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
}
