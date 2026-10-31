import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Label + value + slider. Emits moved(value) while dragging.
ColumnLayout {
    id: sr
    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property string unit: ""
    property bool changed: false
    property string badge: ""
    property color badgeColor: Theme.warn
    signal moved(real value)
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Lbl { text: sr.label }
        Dot { visible: sr.changed; color: Theme.accent; size: 6 }
        Item { Layout.fillWidth: true }
        Lbl { visible: sr.badge.length > 0; text: sr.badge; color: sr.badgeColor; font.pixelSize: 11 }
        Mono { text: Math.round(sr.value) + sr.unit; font.pixelSize: 13 }
    }
    Slider {
        id: sl
        Layout.fillWidth: true
        from: sr.from
        to: sr.to
        stepSize: sr.stepSize
        value: sr.value
        Accessible.name: sr.label
        onMoved: sr.moved(value)
        background: Rectangle {
            x: sl.leftPadding
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth
            height: 4
            radius: 2
            color: Theme.border
            Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: 18; height: 18; radius: 9
            color: Theme.knob
            border.width: sl.visualFocus ? 2 : 1
            border.color: Theme.accent
        }
    }
}
