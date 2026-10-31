import QtQuick
import Corinth

// On-screen joystick: drag the knob to pan/tilt, release to stop.
// Arrow keys work while it has focus. Emits x/y in -1..1 (up = +y).
Item {
    id: js
    property real jx: 0
    property real jy: 0
    property bool dragging: false
    readonly property real reach: width * 0.32
    readonly property bool active: dragging || jx !== 0 || jy !== 0
    signal moved(real x, real y)
    signal released()

    implicitWidth: 196
    implicitHeight: 196
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: "Virtual joystick. Drag to pan and tilt, or hold the arrow keys."

    function setFrom(px, py) {
        var dx = (px - width / 2) / reach
        var dy = -(py - height / 2) / reach
        var m = Math.sqrt(dx * dx + dy * dy)
        if (m > 1) { dx /= m; dy /= m }
        jx = dx; jy = dy
        js.moved(jx, jy)
    }
    function center() {
        var was = active
        dragging = false
        jx = 0; jy = 0
        if (was) js.released()
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.s2
        border.width: js.activeFocus ? 2 : 1
        border.color: js.active || js.activeFocus ? Theme.accent : Theme.border
    }
    Rectangle {
        anchors.centerIn: parent
        width: js.reach * 2; height: width; radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        opacity: 0.8
    }
    Repeater {
        model: 4
        Rectangle {
            required property int index
            width: 2; height: 10; radius: 1
            color: Theme.text2
            x: js.width / 2 - 1 + Math.sin(index * Math.PI / 2) * (js.width / 2 - 14)
            y: js.height / 2 - 5 - Math.cos(index * Math.PI / 2) * (js.height / 2 - 14)
            rotation: index * 90
        }
    }
    Rectangle {
        id: knob
        width: js.width * 0.31; height: width; radius: width / 2
        x: js.width / 2 - width / 2 + js.jx * js.reach
        y: js.height / 2 - height / 2 - js.jy * js.reach
        color: js.active ? Theme.primary : Theme.surface
        border.width: 1
        border.color: Theme.accent
        Behavior on x { enabled: !js.dragging; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on y { enabled: !js.dragging; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        cursorShape: Qt.OpenHandCursor
        onPressed: (m) => { js.forceActiveFocus(); js.dragging = true; js.setFrom(m.x, m.y) }
        onPositionChanged: (m) => { if (js.dragging) js.setFrom(m.x, m.y) }
        onReleased: js.center()
        onCanceled: js.center()
    }

    Keys.onPressed: (e) => {
        if (e.isAutoRepeat) { e.accepted = true; return }
        var v = { [Qt.Key_Up]: [0, 0.6], [Qt.Key_Down]: [0, -0.6], [Qt.Key_Left]: [-0.6, 0], [Qt.Key_Right]: [0.6, 0] }[e.key]
        if (v) { jx = v[0]; jy = v[1]; js.moved(jx, jy); e.accepted = true }
    }
    Keys.onReleased: (e) => {
        if (e.isAutoRepeat) { e.accepted = true; return }
        if (e.key === Qt.Key_Up || e.key === Qt.Key_Down || e.key === Qt.Key_Left || e.key === Qt.Key_Right) { center(); e.accepted = true }
    }
}
