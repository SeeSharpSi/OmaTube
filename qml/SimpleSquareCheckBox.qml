import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    spacing: 8

    PointingCursor {}

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        y: control.height / 2 - height / 2
        border.width: control.checked || control.hovered ? 1.5 : 1
        border.color: control.checked
            ? control.palette.highlight
            : (control.hovered ? control.palette.windowText : control.palette.mid)
        color: "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            visible: control.checked
            color: control.palette.highlight
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.palette.windowText : control.palette.mid
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
