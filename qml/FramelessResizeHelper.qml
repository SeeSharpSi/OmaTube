import QtQuick
import QtQuick.Window

Item {
    id: root

    property Window targetWindow
    readonly property int edge: 6

    anchors.fill: parent
    z: 100

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.edge
        height: root.edge
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.targetWindow.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.edge
        anchors.rightMargin: root.edge
        height: root.edge
        cursorShape: Qt.SizeVerCursor
        onPressed: root.targetWindow.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.edge
        height: root.edge
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.targetWindow.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edge
        anchors.bottomMargin: root.edge
        width: root.edge
        cursorShape: Qt.SizeHorCursor
        onPressed: root.targetWindow.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edge
        anchors.bottomMargin: root.edge
        width: root.edge
        cursorShape: Qt.SizeHorCursor
        onPressed: root.targetWindow.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.edge
        height: root.edge
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.edge
        anchors.rightMargin: root.edge
        height: root.edge
        cursorShape: Qt.SizeVerCursor
        onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.edge
        height: root.edge
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.targetWindow.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
