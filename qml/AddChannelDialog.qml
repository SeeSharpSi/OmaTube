pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Dialog {
    id: root

    readonly property var themeColors: App.themeColors
    readonly property color mutedInk: themeColors.dark_foreground
    readonly property color danger: themeColors.red
    property var selectedCategoryIds: []

    title: qsTr("Add channel")
    modal: true
    width: Math.min(parent ? parent.width - 48 : 520, 520)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape

    function openForCategory(categoryId) {
        root.selectedCategoryIds = categoryId >= 0 ? [categoryId] : []
        channelInput.clear()
        App.clearError()
        root.open()
        channelInput.forceActiveFocus()
    }

    Connections {
        target: App

        function onChannelAdded(title) {
            root.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: qsTr("Paste a youtube.com/@handle URL, @handle, or UC channel ID.")
            wrapMode: Text.Wrap
        }

        TextField {
            id: channelInput
            Layout.fillWidth: true
            placeholderText: qsTr("@channel")
            enabled: !App.addingChannel
            onAccepted: addButton.clicked()
        }

        Label {
            Layout.fillWidth: true
            visible: App.errorMessage.length > 0
            text: App.errorMessage
            color: root.danger
            wrapMode: Text.Wrap
        }

        Label {
            text: qsTr("Categories")
            font.bold: true
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: App.categories

                delegate: CheckBox {
                    id: categoryCheck
                    required property var categoryId
                    required property string name

                    text: categoryCheck.name
                    checked: root.selectedCategoryIds.indexOf(categoryCheck.categoryId) !== -1
                    onToggled: {
                        let ids = root.selectedCategoryIds.slice()
                        let position = ids.indexOf(categoryCheck.categoryId)
                        if (categoryCheck.checked && position === -1)
                            ids.push(categoryCheck.categoryId)
                        else if (!categoryCheck.checked && position !== -1)
                            ids.splice(position, 1)
                        root.selectedCategoryIds = ids
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("No category selected means the channel appears only in the unfiltered feed.")
            color: root.mutedInk
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                enabled: !App.addingChannel
                onClicked: root.reject()
            }

            Button {
                id: addButton
                text: App.addingChannel ? qsTr("Looking up channel...") : qsTr("Add")
                enabled: channelInput.text.trim().length > 0 && !App.addingChannel
                onClicked: App.addChannel(channelInput.text, root.selectedCategoryIds)
            }
        }
    }
}
