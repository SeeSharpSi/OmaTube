pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: errorNotifications

    property string errorMessage: ""
    property int timeoutMs: 30000
    property int maximumVisible: 4

    property color cardFill: "#14161b"
    property color cardBorder: "#8c3131"
    property color cardAccent: "#ff5c5c"
    property color textColor: "#ff8080"

    readonly property int count: notifications.count

    signal dismissed(string sourceMessage)

    implicitWidth: parent ? Math.max(0, Math.min(400, parent.width - 36)) : 400
    implicitHeight: stack.implicitHeight
    width: parent ? Math.max(0, Math.min(400, parent.width - 36)) : 400
    height: stack.implicitHeight

    function dismiss(index) {
        if (index < 0 || index >= notifications.count)
            return
        const message = notifications.get(index).message
        notifications.remove(index)
        dismissed(message)
    }

    function clear() {
        notifications.clear()
    }

    onErrorMessageChanged: pushError(errorMessage)

    function pushError(message) {
        if (message.length === 0)
            return
        for (let i = 0; i < notifications.count; ++i) {
            if (notifications.get(i).message === message)
                return
        }
        if (maximumVisible <= 0)
            return
        while (notifications.count >= maximumVisible)
            notifications.remove(0)
        notifications.append({ message: message })
    }

    ListModel { id: notifications }

    Column {
        id: stack
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: parent.width
        spacing: 8

        Repeater {
            model: notifications

            delegate: Rectangle {
                id: card
                required property int index
                required property string message

                width: stack.width
                height: cardText.implicitHeight + 18
                color: errorNotifications.cardFill
                border.width: 1
                border.color: errorNotifications.cardBorder

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    width: 3
                    color: errorNotifications.cardAccent
                }

                Text {
                    id: cardText
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.topMargin: 9
                    anchors.leftMargin: 10
                    anchors.rightMargin: 20
                    text: card.message
                    color: errorNotifications.textColor
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    maximumLineCount: 6
                    elide: Text.ElideRight
                }

                Text {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    text: "x"
                    color: errorNotifications.textColor
                    font.family: "monospace"
                    font.pixelSize: 12
                    opacity: cardHover.hovered ? 1.0 : 0.6
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) { mouse.accepted = true; errorNotifications.dismiss(card.index) }
                }

                HoverHandler {
                    id: cardHover
                    cursorShape: Qt.PointingHandCursor
                }

                Timer {
                    interval: Math.max(0, errorNotifications.timeoutMs)
                    running: true
                    onTriggered: errorNotifications.dismiss(card.index)
                }
            }
        }
    }
}
