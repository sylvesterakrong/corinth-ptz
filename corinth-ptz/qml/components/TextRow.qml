import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Label + text field.
ColumnLayout {
    id: tf
    property string label: ""
    property string value: ""
    property string placeholder: ""
    property bool password: false
    property bool mono: true
    property bool changed: false
    property bool restart: false
    signal edited(string value)
    spacing: 6

    RowLayout {
        spacing: 6
        Lbl { text: tf.label }
        Dot { visible: tf.changed; color: Theme.accent; size: 6 }
        Lbl { visible: tf.restart; text: "· restart"; color: Theme.warn; font.pixelSize: 10 }
    }
    TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 36
        text: tf.value
        placeholderText: tf.placeholder
        placeholderTextColor: Theme.text2
        echoMode: tf.password ? TextInput.Password : TextInput.Normal
        color: Theme.text
        font.family: tf.mono ? Theme.monoFont : Theme.uiFont
        font.pixelSize: 12
        leftPadding: 10
        selectByMouse: true
        Accessible.name: tf.label
        onTextEdited: tf.edited(text)
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.s2
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus || tf.changed ? Theme.accent : Theme.border
        }
    }
}
