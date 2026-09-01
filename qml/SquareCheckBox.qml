import QtQuick
import QtQuick.Controls
import YtClient

CheckBox {
    id: control

    spacing: 8
    font.family: "monospace"
    font.pixelSize: 12

    readonly property color themeAccent: App.themeColors.accent
    readonly property color themeInk: App.themeColors.foreground
    readonly property color themeMuted: App.themeColors.muted
    readonly property color themeSelection: App.themeColors.selection

    PointingCursor {}

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        y: control.height / 2 - height / 2
        border.width: control.checked || control.hovered ? 2 : 1
        border.color: control.checked
            ? control.themeAccent
            : (control.hovered ? control.themeInk : control.themeMuted)
        color: control.checked ? control.themeSelection : "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: 7
            height: 7
            visible: control.checked
            color: control.themeAccent
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.themeInk : control.themeMuted
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
