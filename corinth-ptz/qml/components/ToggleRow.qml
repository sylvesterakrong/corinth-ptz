import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Full-width toggle row: label, optional hint, switch. Shows a dot when changed.
Button {
    id: tr
    property string label: ""
    property string hint: ""
    property bool isOn: false
    property bool changed: false
    property bool warnWhenOn: false
    property bool unknown: false      // state not read from the device: show the pill dimmed
    signal toggledByUser(bool value)

    implicitHeight: hint.length ? 52 : 44
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checked: isOn
    onClicked: toggledByUser(!isOn)

    contentItem: RowLayout {
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                spacing: 6
                Lbl { text: tr.label; color: Theme.text; Layout.fillWidth: false }
                Dot { visible: tr.changed; color: Theme.accent; size: 6 }
            }
            Text {
                visible: tr.hint.length > 0
                text: tr.hint
                font.family: Theme.uiFont
                font.pixelSize: 11
                color: tr.warnWhenOn && tr.isOn ? Theme.warn : Theme.text2
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        SwitchPill { checked: tr.isOn; opacity: tr.unknown ? 0.4 : 1 }
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.s2
        border.width: 1
        border.color: tr.changed ? Theme.accent : Theme.border
        Rectangle { anchors.fill: parent; radius: parent.radius; color: "#7f7f7f"; opacity: tr.down ? 0.16 : (tr.hovered ? 0.08 : 0) }
        Rectangle { anchors.fill: parent; anchors.margins: -3; radius: parent.radius + 3; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: tr.visualFocus }
    }
}
