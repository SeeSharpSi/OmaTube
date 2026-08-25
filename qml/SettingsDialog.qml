pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import YtClient

Dialog {
    id: root

    title: qsTr("YouTube API key")
    modal: true
    width: Math.min(parent ? parent.width - 48 : 540, 540)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape

    onOpened: {
        keyInput.clear()
        keyInput.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: App.apiKeyConfigured
                ? qsTr("An API key is configured. Enter another key to replace it.")
                : qsTr("Enter your own YouTube Data API v3 key. This is not a YouTube login.")
            wrapMode: Text.Wrap
        }

        TextField {
            id: keyInput
            Layout.fillWidth: true
            placeholderText: qsTr("API key")
            echoMode: TextInput.Password
            selectByMouse: true
            onAccepted: saveButton.clicked()
        }

        CheckBox {
            id: rememberKey
            text: qsTr("Remember in this app's local settings")
            checked: false
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Remembering stores the key as plain local configuration. Leave unchecked or use the YT_CLIENT_API_KEY environment variable to avoid that.")
            color: "#6e6960"
            font.pixelSize: 12
            wrapMode: Text.Wrap
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

            Button {
                text: qsTr("Remove stored key")
                visible: App.apiKeyConfigured
                onClicked: {
                    App.clearApiKey()
                    root.close()
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                onClicked: root.reject()
            }

            Button {
                id: saveButton
                text: qsTr("Use key")
                enabled: keyInput.text.trim().length > 0
                onClicked: {
                    if (App.setApiKey(keyInput.text, rememberKey.checked))
                        root.accept()
                }
            }
        }
    }
}
