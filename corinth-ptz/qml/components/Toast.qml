import QtQuick
import QtQuick.Layouts
import Corinth

// Floating notice card with optional action buttons (put them inside).
Card {
    id: toast
    property string title: ""
    property string detail: ""
    property color edge: Theme.accent
    default property alias actions: actionRow.data
    signal dismissed()

    width: 360
    implicitHeight: col.implicitHeight + 32
    border.color: edge
    z: 50

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 32; height: 32; radius: 8
                color: Theme.accentSoft
                Text { anchors.centerIn: parent; text: "◉"; color: Theme.accent; font.pixelSize: 14 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Lbl { text: toast.title; color: Theme.text; font.pixelSize: 13 }
                Text { text: toast.detail; color: Theme.text2; font.family: Theme.uiFont; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
            Btn { text: "✕"; variant: "ghost"; compact: true; implicitWidth: 30; onClicked: toast.dismissed(); Accessible.name: "Dismiss" }
        }
        RowLayout { id: actionRow; Layout.fillWidth: true; spacing: 8; visible: children.length > 0 }
    }
}
