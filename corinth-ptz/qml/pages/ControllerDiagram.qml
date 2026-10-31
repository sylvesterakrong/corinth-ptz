import QtQuick
import Corinth

// Live controller diagram for the selected device type. Pressed inputs light
// up in the accent colour; sticks move with the axes.
Item {
    id: dg
    property string diagram: "pad"          // pad | flight | kb | generic
    property var pressed: ({})
    property var axes: [0, 0, 0, 0, 0, 0]  // a0 a1 a2 a3 lt rt
    property var names: ({})                // input id -> label (pad face buttons)

    implicitWidth: 520
    implicitHeight: width * 300 / 520

    function on(id) { return !!pressed[id] }
    function fill(id) { return on(id) ? Theme.accent : Theme.surface }
    function ax(i) { return axes && axes.length > i ? Number(axes[i]) : 0 }

    // Inline component: it cannot see `dg`, so callers pass `lit`.
    component Key: Rectangle {
        property string inputId
        property string caption: ""
        property bool lit: false
        radius: 5
        color: lit ? Theme.accent : Theme.surface
        border.width: 1
        border.color: Theme.border
        Text { anchors.centerIn: parent; text: parent.caption; color: Theme.text; font.family: Theme.monoFont; font.pixelSize: 10 }
    }

    Item {
        width: 520
        height: 300
        scale: dg.width / 520
        transformOrigin: Item.TopLeft

        // ---------------- gamepad ----------------
        Item {
            anchors.fill: parent
            visible: dg.diagram === "pad"
            Rectangle { x: 20; y: 70; width: 480; height: 200; radius: 100; color: Theme.s2; border.color: Theme.border; border.width: 1.5 }
            Key { inputId: "lt"; lit: dg.on("lt"); caption: dg.names.lt || "LT"; x: 92; y: 30; width: 86; height: 22; radius: 8 }
            Key { inputId: "rt"; lit: dg.on("rt"); caption: dg.names.rt || "RT"; x: 342; y: 30; width: 86; height: 22; radius: 8 }
            Key { inputId: "lb"; lit: dg.on("lb"); caption: dg.names.lb || "LB"; x: 96; y: 56; width: 78; height: 16; radius: 8 }
            Key { inputId: "rb"; lit: dg.on("rb"); caption: dg.names.rb || "RB"; x: 346; y: 56; width: 78; height: 16; radius: 8 }
            Rectangle { x: 96; y: 96; width: 68; height: 68; radius: 34; color: Theme.surface; border.width: 1.5; border.color: (dg.ax(0) || dg.ax(1)) ? Theme.accent : Theme.border }
            Rectangle { x: 130 - 18 + dg.ax(0) * 16; y: 130 - 18 + dg.ax(1) * 16; width: 36; height: 36; radius: 18; color: dg.on("l3") ? Theme.accent : Theme.primary; border.color: Theme.accent }
            Rectangle { x: 290; y: 170; width: 60; height: 60; radius: 30; color: Theme.surface; border.width: 1.5; border.color: (dg.ax(2) || dg.ax(3)) ? Theme.accent : Theme.border }
            Rectangle { x: 320 - 16 + dg.ax(2) * 14; y: 200 - 16 + dg.ax(3) * 14; width: 32; height: 32; radius: 16; color: dg.on("r3") ? Theme.accent : Theme.primary; border.color: Theme.accent }
            Key { inputId: "up"; lit: dg.on("up"); x: 186; y: 170; width: 20; height: 20; radius: 3 }
            Key { inputId: "down"; lit: dg.on("down"); x: 186; y: 210; width: 20; height: 20; radius: 3 }
            Key { inputId: "left"; lit: dg.on("left"); x: 166; y: 190; width: 20; height: 20; radius: 3 }
            Key { inputId: "right"; lit: dg.on("right"); x: 206; y: 190; width: 20; height: 20; radius: 3 }
            Key { inputId: "y"; lit: dg.on("y"); caption: dg.names.y === "Triangle" ? "△" : "Y"; x: 387; y: 87; width: 26; height: 26; radius: 13 }
            Key { inputId: "x"; lit: dg.on("x"); caption: dg.names.x === "Square" ? "□" : "X"; x: 363; y: 111; width: 26; height: 26; radius: 13 }
            Key { inputId: "b"; lit: dg.on("b"); caption: dg.names.b === "Circle" ? "○" : "B"; x: 411; y: 111; width: 26; height: 26; radius: 13 }
            Key { inputId: "a"; lit: dg.on("a"); caption: dg.names.a === "Cross" ? "✕" : "A"; x: 387; y: 135; width: 26; height: 26; radius: 13 }
            Key { inputId: "view"; lit: dg.on("view"); x: 220; y: 118; width: 26; height: 14; radius: 7 }
            Key { inputId: "menu"; lit: dg.on("menu"); x: 274; y: 118; width: 26; height: 14; radius: 7 }
        }

        // ---------------- flight stick ----------------
        Item {
            anchors.fill: parent
            visible: dg.diagram === "flight"
            Rectangle { x: 90; y: 224; width: 340; height: 60; radius: 16; color: Theme.s2; border.color: Theme.border; border.width: 1.5 }
            Repeater {
                model: [["b7", 121, 235], ["b9", 149, 235], ["b11", 177, 235], ["b8", 121, 257], ["b10", 149, 257], ["b12", 177, 257]]
                Key { required property var modelData; inputId: modelData[0]; lit: dg.on(modelData[0]); caption: modelData[0].substr(1); x: modelData[1]; y: modelData[2]; width: 18; height: 18; radius: 9 }
            }
            Rectangle { x: 320; y: 250; width: 80; height: 6; radius: 3; color: Theme.border }
            Rectangle { x: 320 + (dg.ax(3) + 1) / 2 * 66; y: 242; width: 14; height: 22; radius: 4; color: dg.ax(3) ? Theme.accent : Theme.surface; border.color: Theme.accent }
            Text { x: 340; y: 268; text: "Throttle"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
            Rectangle { x: 257 + dg.ax(0) * 9; y: 170; width: 6; height: 56; radius: 3; color: Theme.text2; rotation: dg.ax(0) * 12; transformOrigin: Item.Bottom }
            Item {
                x: dg.ax(0) * 18
                y: dg.ax(1) * 8
                width: 520; height: 300
                Rectangle { x: 226; y: 60; width: 68; height: 118; radius: 26; color: Theme.s2; border.width: 1.5; border.color: (dg.ax(0) || dg.ax(1)) ? Theme.accent : Theme.border }
                Key { inputId: "trigger"; lit: dg.on("trigger"); x: 214; y: 104; width: 14; height: 34; radius: 6 }
                Key { inputId: "thumb"; lit: dg.on("thumb"); x: 235; y: 71; width: 18; height: 18; radius: 9 }
                Key { inputId: "b3"; lit: dg.on("b3"); caption: "3"; x: 268; y: 70; width: 16; height: 16; radius: 8 }
                Key { inputId: "b4"; lit: dg.on("b4"); caption: "4"; x: 268; y: 92; width: 16; height: 16; radius: 8 }
                Key { inputId: "b5"; lit: dg.on("b5"); caption: "5"; x: 237; y: 95; width: 14; height: 14; radius: 7 }
                Key { inputId: "b6"; lit: dg.on("b6"); caption: "6"; x: 253; y: 115; width: 14; height: 14; radius: 7 }
                Key { inputId: "hatU"; lit: dg.on("hatU"); x: 252; y: 40; width: 16; height: 10; radius: 2 }
                Key { inputId: "hatL"; lit: dg.on("hatL"); x: 236; y: 52; width: 10; height: 10; radius: 2 }
                Key { inputId: "hatR"; lit: dg.on("hatR"); x: 274; y: 52; width: 10; height: 10; radius: 2 }
                Key { inputId: "hatD"; lit: dg.on("hatD"); x: 254; y: 52; width: 12; height: 8; radius: 2 }
            }
            Rectangle { x: 258.5; y: 12; width: 3; height: 18; radius: 1.5; color: dg.ax(2) ? Theme.accent : Theme.text2; rotation: dg.ax(2) * 40; transformOrigin: Item.Center }
            Text { x: 300; y: 14; text: "Twist"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
            Text { x: 162; y: 114; text: "Trigger"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
        }

        // ---------------- PTZ keyboard ----------------
        Item {
            anchors.fill: parent
            visible: dg.diagram === "kb"
            Rectangle { x: 10; y: 30; width: 500; height: 250; radius: 18; color: Theme.s2; border.color: Theme.border; border.width: 1.5 }
            Rectangle { x: 30; y: 50; width: 250; height: 26; radius: 4; color: Theme.surface; border.color: Theme.border
                Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "CAM 1 · PTZ"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 } }
            Repeater {
                model: [["irisO", "Iris +", 30, 92, 72], ["irisC", "Iris −", 110, 92, 72], ["lock", "Lock", 190, 92, 90],
                        ["focN", "Near", 30, 130, 72], ["focF", "Far", 110, 130, 72], ["prev", "Prev", 190, 130, 42], ["next", "Next", 238, 130, 42],
                        ["f1", "F1", 30, 176, 72], ["f2", "F2", 110, 176, 72], ["f3", "F3", 190, 176, 90],
                        ["f4", "F4", 30, 214, 72], ["f5", "F5", 110, 214, 72], ["f6", "F6", 190, 214, 90]]
                Key { required property var modelData; inputId: modelData[0]; lit: dg.on(modelData[0]); caption: modelData[1]; x: modelData[2]; y: modelData[3]; width: modelData[4]; height: 30 }
            }
            Rectangle { x: 320; y: 75; width: 160; height: 160; radius: 80; color: Theme.surface; border.width: 1.5; border.color: (dg.ax(0) || dg.ax(1)) ? Theme.accent : Theme.border }
            Rectangle { x: 400 - 28 + dg.ax(0) * 30; y: 155 - 28 + dg.ax(1) * 30; width: 56; height: 56; radius: 28; color: Theme.primary; border.color: Theme.accent }
            Item {
                x: 400; y: 155; rotation: dg.ax(2) * 40
                Rectangle { x: -1.5; y: -93; width: 3; height: 16; radius: 1.5; color: dg.ax(2) ? Theme.accent : Theme.text2 }
            }
            Text { x: 330; y: 252; text: "3-axis joystick · twist = zoom"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
        }

        // ---------------- generic ----------------
        Item {
            anchors.fill: parent
            visible: dg.diagram === "generic"
            Repeater {
                model: [[20, 0, 1, "Axes 0/1"], [170, 2, 3, "Axes 2/3"]]
                Item {
                    required property var modelData
                    x: modelData[0]; y: 40; width: 130; height: 160
                    Rectangle { width: 130; height: 130; radius: 12; color: Theme.s2; border.width: 1.5; border.color: (dg.ax(modelData[1]) || dg.ax(modelData[2])) ? Theme.accent : Theme.border }
                    Rectangle { x: 64; y: 10; width: 1; height: 110; color: Theme.border }
                    Rectangle { x: 10; y: 64; width: 110; height: 1; color: Theme.border }
                    Rectangle { x: 65 - 8 + dg.ax(modelData[1]) * 50; y: 65 - 8 + dg.ax(modelData[2]) * 50; width: 16; height: 16; radius: 8; color: Theme.accent }
                    Text { y: 140; width: 130; horizontalAlignment: Text.AlignHCenter; text: modelData[3]; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
                }
            }
            Repeater {
                model: 12
                Key {
                    required property int index
                    inputId: "b" + (index + 1)
                    lit: dg.on("b" + (index + 1))
                    caption: String(index + 1)
                    x: 325 + (index % 4) * 44
                    y: 45 + Math.floor(index / 4) * 44
                    width: 30; height: 30; radius: 15
                }
            }
            Key { inputId: "hatU"; lit: dg.on("hatU"); x: 388; y: 196; width: 24; height: 18; radius: 3 }
            Key { inputId: "hatD"; lit: dg.on("hatD"); x: 388; y: 238; width: 24; height: 18; radius: 3 }
            Key { inputId: "hatL"; lit: dg.on("hatL"); x: 360; y: 217; width: 24; height: 18; radius: 3 }
            Key { inputId: "hatR"; lit: dg.on("hatR"); x: 416; y: 217; width: 24; height: 18; radius: 3 }
            Text { x: 390; y: 262; text: "Hat"; color: Theme.text2; font.family: Theme.monoFont; font.pixelSize: 10 }
        }
    }
}
