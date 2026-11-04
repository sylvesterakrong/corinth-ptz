import QtQuick
import QtQuick.Controls.Basic
import Corinth

// Segmented control. model: [{ id: "main", label: "Main" }, ...]
Rectangle {
    id: seg
    property var model: []
    property string current: ""
    property bool onPage: false
    property bool small: false
    signal picked(string id)

    implicitHeight: small ? 30 : 34
    implicitWidth: row.implicitWidth + 6
    radius: Theme.radiusControl
    color: onPage ? Theme.pageS2 : Theme.s2
    border.width: 1
    border.color: onPage ? Theme.pageBorder : Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: seg.model
            delegate: Button {
                id: sb
                required property var modelData
                readonly property bool sel: seg.current === modelData.id
                text: modelData.label
                implicitHeight: seg.small ? 24 : 28
                leftPadding: seg.model.length > 5 ? 6 : 10
                rightPadding: seg.model.length > 5 ? 6 : 10
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.Medium
                Accessible.name: text
                onClicked: seg.picked(modelData.id)
                contentItem: Text {
                    text: sb.text
                    font: sb.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: sb.sel ? (seg.onPage ? Theme.pageSegOnText : Theme.text)
                                  : (seg.onPage ? Theme.pageText2 : Theme.text2)
                }
                background: Rectangle {
                    radius: Theme.radiusInner
                    color: sb.sel ? (seg.onPage ? Theme.pageSegOn : Theme.surface) : "transparent"
                    Rectangle { anchors.fill: parent; radius: parent.radius; color: "#7f7f7f"; opacity: sb.down ? 0.16 : (sb.hovered && !sb.sel ? 0.08 : 0) }
                    Rectangle { anchors.fill: parent; anchors.margins: -2; radius: parent.radius + 2; color: "transparent"; border.width: 2; border.color: seg.onPage ? Theme.pageAccent : Theme.accent; visible: sb.visualFocus }
                }
            }
        }
    }
}
