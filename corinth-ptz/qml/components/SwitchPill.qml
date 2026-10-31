import QtQuick
import Corinth

// Visual on/off pill (the parent control handles input and accessibility).
Rectangle {
    property bool checked: false
    implicitWidth: 36
    implicitHeight: 20
    radius: height / 2
    color: checked ? Theme.accent : Theme.border
    Behavior on color { ColorAnimation { duration: 140 } }
    Rectangle {
        width: parent.height - 4
        height: width
        radius: width / 2
        y: 2
        x: parent.checked ? parent.width - width - 2 : 2
        color: Theme.knob
        Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }
}
