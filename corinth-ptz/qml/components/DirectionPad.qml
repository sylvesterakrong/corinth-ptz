import QtQuick
import QtQuick.Controls.Basic
import Corinth

// 8-direction pad with a centre stop. Hold to move, release to stop.
Rectangle {
    id: pad
    signal pressedDir(real dx, real dy)
    signal releasedDir()
    signal stopClicked()

    implicitWidth: 204
    implicitHeight: 204
    radius: width / 2
    color: Theme.s2
    border.width: 1
    border.color: Theme.border

    readonly property var dirs: [
        { label: "Up left", dx: -1, dy: 1, rot: -45 }, { label: "Up", dx: 0, dy: 1, rot: 0 }, { label: "Up right", dx: 1, dy: 1, rot: 45 },
        { label: "Left", dx: -1, dy: 0, rot: -90 }, { label: "Stop", dx: 0, dy: 0, rot: 0 }, { label: "Right", dx: 1, dy: 0, rot: 90 },
        { label: "Down left", dx: -1, dy: -1, rot: -135 }, { label: "Down", dx: 0, dy: -1, rot: 180 }, { label: "Down right", dx: 1, dy: -1, rot: 135 }
    ]

    Grid {
        anchors.fill: parent
        anchors.margins: 12
        columns: 3
        spacing: 6
        Repeater {
            model: pad.dirs
            delegate: Button {
                id: db
                required property var modelData
                readonly property bool isStop: modelData.label === "Stop"
                width: (pad.width - 24 - 12) / 3
                height: width
                hoverEnabled: true
                Accessible.name: modelData.label
                onPressed: if (!isStop) pad.pressedDir(modelData.dx * (modelData.dx && modelData.dy ? 0.7071 : 1), modelData.dy * (modelData.dx && modelData.dy ? 0.7071 : 1))
                onReleased: if (!isStop) pad.releasedDir()
                onCanceled: if (!isStop) pad.releasedDir()
                onClicked: if (isStop) pad.stopClicked()
                contentItem: Item {
                    Rectangle { visible: db.isStop; anchors.centerIn: parent; width: 12; height: 12; radius: 3; color: db.down ? Theme.accent : Theme.crit }
                    Text {
                        visible: !db.isStop
                        anchors.centerIn: parent
                        text: "▲"
                        rotation: db.modelData.rot
                        font.pixelSize: 15
                        color: db.down ? Theme.accent : Theme.text
                    }
                }
                background: Rectangle {
                    radius: db.isStop ? width / 2 : 10
                    color: db.down ? Theme.accentSoft : (db.isStop ? Theme.surface : "transparent")
                    border.width: 1
                    border.color: db.down ? Theme.accent : (db.isStop ? Theme.border : "transparent")
                    Rectangle { anchors.fill: parent; radius: parent.radius; color: "#7f7f7f"; opacity: db.hovered && !db.down ? 0.08 : 0 }
                    Rectangle { anchors.fill: parent; anchors.margins: -2; radius: parent.radius + 2; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: db.visualFocus }
                }
            }
        }
    }
}
