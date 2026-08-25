pragma ComponentBehavior: Bound
// C++ list models provide roles dynamically through roleNames().
// qmllint disable unresolved-type

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Dialog {
    id: root

    property string pendingAction: ""
    property var pendingId: -1
    property string pendingName: ""

    title: qsTr("Manage subscriptions")
    modal: true
    width: Math.min(parent ? parent.width - 48 : 760, 760)
    height: Math.min(parent ? parent.height - 48 : 620, 620)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape

    function confirmDelete(action, id, name) {
        pendingAction = action
        pendingId = id
        pendingName = name
        confirmDialog.open()
    }

    contentItem: ColumnLayout {
        spacing: 10

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: qsTr("Categories") }
            TabButton { text: qsTr("Channels") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            id: newCategoryInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("New category")
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
                        spacing: 6
                        model: App.categories

                        delegate: Rectangle {
                            id: categoryDelegate
                            required property var categoryId
                            required property string name

                            width: ListView.view.width
                            height: 48
                            radius: 4
                            color: "#f5f1e8"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 6

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
                                    onClicked: root.confirmDelete(
                                        "category", categoryDelegate.categoryId, categoryDelegate.name)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("No categories yet.")
                            visible: parent.count === 0
                        }
                    }
                }
            }

            Item {
                ListView {
                    anchors.fill: parent
                    clip: true
                    spacing: 8
                    model: App.channels

                    delegate: Rectangle {
                        id: channelDelegate
                        required property string channelId
                        required property string title
                        required property string handle
                        required property var categoryIds

                        width: ListView.view.width
                        height: channelContent.implicitHeight + 20
                        radius: 4
                        color: "#f5f1e8"

                        ColumnLayout {
                            id: channelContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        text: channelDelegate.title
                                        font.bold: true
                                    }

                                    Label {
                                        text: channelDelegate.handle
                                        color: "#6e6960"
                                        font.pixelSize: 12
                                    }
                                }

                                Button {
                                    text: qsTr("Remove")
                                    onClicked: root.confirmDelete(
                                        "channel", channelDelegate.channelId, channelDelegate.title)
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: App.categories

                                    delegate: CheckBox {
                                        required property var categoryId
                                        required property string name

                                        text: name
                                        checked: channelDelegate.categoryIds.indexOf(categoryId) !== -1
                                        onToggled: App.setChannelInCategory(
                                            channelDelegate.channelId, categoryId, checked)
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        text: qsTr("No channels yet. Close this window and add one.")
                        visible: parent.count === 0
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: App.errorMessage.length > 0
            text: App.errorMessage
            color: "#8a2525"
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
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
                ? qsTr("Delete '%1'? Channels and cached videos are kept.").arg(root.pendingName)
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
