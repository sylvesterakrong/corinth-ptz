import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Joystick setup: device type, live diagram and axes, response curve, assignments.
Rectangle {
    id: root
    color: Theme.pageBg
    property bool active: false
    signal back()
    signal openSettings()

    property string listenAction: ""
    property string notice: ""
    property bool noticeWarn: false

    readonly property var currentType: {
        var t = joystick.types
        for (var i = 0; i < t.length; ++i) if (t[i].id === joystick.deviceType) return t[i]
        return t[0]
    }
    readonly property var padNames: {
        var n = {}
        var ids = ["a", "b", "x", "y", "lb", "rb", "lt", "rt"]
        for (var i = 0; i < ids.length; ++i) n[ids[i]] = joystick.label(ids[i])
        return n
    }
    readonly property var axisRows: {
        var a = joystick.axes
        var d = currentType ? currentType.diagram : "pad"
        if (d === "pad") return [["Left X", a[0], false], ["Left Y", a[1], false], ["Right X", a[2], false], ["Right Y", a[3], false], [joystick.label("lt"), a[4], true], [joystick.label("rt"), a[5], true]]
        if (d === "flight") return [["Stick X", a[0], false], ["Stick Y", a[1], false], ["Twist", a[2], false], ["Throttle", a[3], false]]
        if (d === "kb") return [["Stick X", a[0], false], ["Stick Y", a[1], false], ["Twist", a[2], false]]
        return [["Axis 0", a[0], false], ["Axis 1", a[1], false], ["Axis 2", a[2], false], ["Axis 3", a[3], false]]
    }

    function startListen(action) {
        if (listenAction === action) { stopListen(); notice = ""; return }
        listenAction = action
        joystick.listening = true
        noticeWarn = false
        notice = joystick.isAxisAction(action) ? "Move a stick, twist or trigger…" : "Press a button…"
    }
    function stopListen() { listenAction = ""; joystick.listening = false }

    Connections {
        target: joystick
        function onRawInput(input) {
            if (!root.listenAction.length) return
            var r = joystick.setBinding(root.listenAction, input)
            if (r.charAt(0) === "?") { root.notice = r.substr(1); root.noticeWarn = true; return }
            root.noticeWarn = r.charAt(0) === "!"
            root.notice = root.noticeWarn ? r.substr(1) : r
            root.stopListen()
        }
    }
    onActiveChanged: if (!active) stopListen()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: Theme.pageBg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.pageBorder }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 12
                Btn { onPage: true; text: "‹  Live view"; onClicked: root.back() }
                Btn { onPage: true; text: "All settings"; onClicked: root.openSettings() }
                ColumnLayout {
                    spacing: 0
                    Lbl { text: "Joystick input"; onPage: true; color: Theme.pageText; font.pixelSize: 16; font.letterSpacing: 3.2 }
                    Text { text: "Settings › App › Input · saved on this laptop · backend " + joystick.backend; color: Theme.pageText2; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
                Seg {
                    onPage: true; small: true
                    model: [{ id: "light", label: "Day" }, { id: "dark", label: "Dark" }, { id: "night", label: "Night" }]
                    current: app.theme
                    onPicked: (id) => app.theme = id
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body
                x: 24
                y: 20
                width: Math.min(parent.width - 48, 1400)
                spacing: 16

                // ---------- detected devices ----------
                Card {
                    Layout.fillWidth: true
                    implicitHeight: devCol.implicitHeight + 32
                    ColumnLayout {
                        id: devCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 16
                        anchors.leftMargin: 20; anchors.rightMargin: 20
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Lbl { text: "Detected devices"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: joystick.devices.length + (joystick.devices.length === 1 ? " device" : " devices"); color: Theme.text2; font.pixelSize: 12 }
                            Btn {
                                visible: joystick.connected && joystick.suggestedType.length > 0 && joystick.suggestedType !== joystick.deviceType
                                variant: "primary"; compact: true
                                text: "Use detected type"
                                onClicked: { root.stopListen(); joystick.useSuggestedType(); root.notice = "Device type set to match " + joystick.deviceName; root.noticeWarn = false }
                            }
                        }
                        Repeater {
                            model: joystick.devices
                            delegate: Btn {
                                required property var modelData
                                readonly property bool inUse: modelData.guid === joystick.selectedGuid
                                Layout.fillWidth: true
                                implicitHeight: 56
                                variant: "toggle"
                                on: inUse
                                text: ""
                                Accessible.name: modelData.name
                                Accessible.role: Accessible.RadioButton
                                onClicked: { root.stopListen(); joystick.selectDevice(modelData.guid); root.notice = "" }
                                contentItem: RowLayout {
                                    spacing: 12
                                    Dot { color: inUse ? Theme.good : Theme.text2; Layout.alignment: Qt.AlignVCenter }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Mono { text: modelData.axes + " axes · " + modelData.buttons + " buttons · " + modelData.hats + (modelData.hats === 1 ? " hat" : " hats"); color: Theme.text2; font.pixelSize: 11 }
                                    }
                                    Lbl { text: modelData.gamepad ? "Gamepad" : "Joystick"; color: Theme.text2; font.pixelSize: 11 }
                                    Lbl { visible: inUse; text: "In use"; color: Theme.good; font.pixelSize: 11 }
                                }
                            }
                        }
                        Text {
                            visible: joystick.devices.length === 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text2
                            font.pixelSize: 12
                            text: joystick.backend === "SDL2"
                                  ? "No joystick found. Plug one in: it appears here at once, no restart needed."
                                  : "This build has no SDL2, so only Xbox-compatible gamepads are seen. Rebuild with SDL2 (it is downloaded automatically) for every other joystick."
                        }
                        Text {
                            visible: joystick.devices.length > 1
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text2
                            font.pixelSize: 11
                            text: "Pick the one that drives the camera. It is remembered, and used again whenever it is plugged in."
                        }
                    }
                }

                // ---------- device type ----------
                Card {
                    Layout.fillWidth: true
                    implicitHeight: typeCol.implicitHeight + 32
                    ColumnLayout {
                        id: typeCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 16
                        anchors.leftMargin: 20; anchors.rightMargin: 20
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Lbl { text: "Device type"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            Rectangle {
                                radius: 999
                                implicitHeight: 28
                                implicitWidth: stRow.implicitWidth + 20
                                color: joystick.connected ? Theme.goodSoft : Theme.accentSoft
                                border.width: 1
                                border.color: joystick.connected ? Theme.good : Theme.accent
                                Row {
                                    id: stRow
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Dot { anchors.verticalCenter: parent.verticalCenter; color: joystick.connected ? Theme.good : Theme.accent }
                                    Text { text: joystick.connected ? "Controller connected" : (joystick.available ? "No controller · simulated" : "Joystick backend not available"); color: Theme.text; font.pixelSize: 12 }
                                }
                            }
                            Btn { variant: "ghost"; compact: true; text: "Restore defaults for this type"; onClicked: { joystick.restoreDefaults(); root.notice = "Defaults restored for " + root.currentType.shortName; root.noticeWarn = false } }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: Math.max(1, Math.floor(width / 220))
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: joystick.types
                                delegate: Btn {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 76
                                    variant: "toggle"
                                    on: joystick.deviceType === modelData.id
                                    text: ""
                                    Accessible.name: modelData.label
                                    Accessible.role: Accessible.RadioButton
                                    onClicked: { root.stopListen(); joystick.deviceType = modelData.id; root.notice = "" }
                                    contentItem: ColumnLayout {
                                        spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: modelData.label; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Lbl { visible: joystick.suggestedType === modelData.id; text: "Detected"; color: Theme.good; font.pixelSize: 10 }
                                        }
                                        Text { text: modelData.desc; color: Theme.text2; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text2
                            font.pixelSize: 12
                            text: joystick.connected
                                  ? "Connected: " + joystick.deviceName + (joystick.standardMapping ? " · standard gamepad mapping" : " · joystick mapping, check each assignment") +
                                    (joystick.suggestedType.length && joystick.suggestedType !== joystick.deviceType ? " · looks like a " + joystick.suggestedType + " device" : "")
                                  : (joystick.backend === "XInput"
                                     ? "No controller detected. XInput supports Xbox-compatible gamepads; build with SDL2 for flight sticks and generic joysticks (see README)."
                                     : "No controller detected. Plug one in; until then use the buttons under the diagram to stand in for the device.")
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    // ---------- diagram ----------
                    Card {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        implicitHeight: diaCol.implicitHeight + 40
                        ColumnLayout {
                            id: diaCol
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 20
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                Lbl { text: root.currentType ? root.currentType.label : ""; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: "Lights up as you press"; color: Theme.text2; font.pixelSize: 11 }
                            }
                            ControllerDiagram {
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 300 / 520
                                diagram: root.currentType ? root.currentType.diagram : "pad"
                                pressed: joystick.pressed
                                axes: joystick.axes
                                names: root.padNames
                            }
                            Text { text: joystick.connected ? "Controller connected. These buttons still work for testing." : "Simulated input: click to stand in for the device."; color: Theme.text2; font.pixelSize: 11 }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: joystick.inputs
                                    delegate: Btn {
                                        required property var modelData
                                        compact: true
                                        variant: "toggle"
                                        on: !!joystick.pressed[modelData.id]
                                        text: modelData.label
                                        font.family: Theme.monoFont
                                        font.pixelSize: 11
                                        onClicked: joystick.simulateInput(modelData.id)
                                    }
                                }
                            }
                        }
                    }

                    // ---------- axes ----------
                    Card {
                        Layout.preferredWidth: 440
                        Layout.alignment: Qt.AlignTop
                        implicitHeight: axCol.implicitHeight + 40
                        ColumnLayout {
                            id: axCol
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 20
                            spacing: 12
                            Lbl { text: "Live axes"; color: Theme.text; font.pixelSize: 13 }
                            Repeater {
                                model: root.axisRows
                                delegate: RowLayout {
                                    required property var modelData
                                    readonly property real shaped: joystick.shape(Number(modelData[1]))
                                    readonly property bool oneSided: modelData[2]
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Lbl { text: modelData[0]; font.pixelSize: 11; Layout.preferredWidth: 80 }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 10
                                        radius: 5
                                        color: Theme.s2
                                        clip: true
                                        readonly property real dz: joystick.deadband / 100
                                        Rectangle {
                                            x: parent.parent.oneSided ? 0 : parent.width * (0.5 - parent.dz / 2)
                                            width: parent.width * (parent.parent.oneSided ? parent.dz : parent.dz)
                                            height: parent.height
                                            color: Theme.border
                                        }
                                        Rectangle {
                                            readonly property real v: parent.parent.shaped
                                            x: parent.parent.oneSided ? 0 : parent.width * (0.5 + Math.min(0, v) / 2)
                                            width: parent.width * (parent.parent.oneSided ? Math.abs(v) : Math.abs(v) / 2)
                                            height: parent.height
                                            radius: 5
                                            color: Theme.accent
                                        }
                                        Rectangle { visible: !parent.parent.oneSided; x: parent.width / 2; width: 1; height: parent.height; color: Theme.text2 }
                                    }
                                    Mono { text: (shaped > 0 ? "+" : shaped < 0 ? "−" : " ") + Math.abs(Math.round(shaped * 100)); Layout.preferredWidth: 44; horizontalAlignment: Text.AlignRight }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 16
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    SliderRow { Layout.fillWidth: true; label: "Deadband"; unit: "%"; from: 0; to: 30; value: joystick.deadband; onMoved: (v) => joystick.deadband = Math.round(v) }
                                    SliderRow { Layout.fillWidth: true; label: "Expo (fine control)"; unit: "%"; from: 0; to: 100; value: joystick.expo; onMoved: (v) => joystick.expo = Math.round(v) }
                                    ToggleRow { Layout.fillWidth: true; label: "Invert tilt"; isOn: joystick.invertTilt; onToggledByUser: (v) => joystick.invertTilt = v }
                                }
                                ColumnLayout {
                                    spacing: 4
                                    Canvas {
                                        id: curve
                                        width: 132; height: 132
                                        property int dead: joystick.deadband
                                        property int expo: joystick.expo
                                        property color stroke: Theme.accent
                                        property color grid: Theme.border
                                        property color bg: Theme.s2
                                        onDeadChanged: requestPaint()
                                        onExpoChanged: requestPaint()
                                        onStrokeChanged: requestPaint()
                                        onPaint: {
                                            var c = getContext("2d")
                                            c.reset()
                                            c.fillStyle = bg; c.fillRect(0, 0, width, height)
                                            var p = 12, w = width - 2 * p, h = height - 2 * p
                                            c.strokeStyle = grid; c.lineWidth = 1
                                            c.beginPath(); c.moveTo(p, p); c.lineTo(p, p + h); c.lineTo(p + w, p + h); c.stroke()
                                            c.setLineDash([3, 4]); c.beginPath(); c.moveTo(p, p + h); c.lineTo(p + w, p); c.stroke(); c.setLineDash([])
                                            c.strokeStyle = stroke; c.lineWidth = 2; c.beginPath()
                                            for (var i = 0; i <= 40; ++i) {
                                                var x = i / 40, y = joystick.shape(x)
                                                var px = p + x * w, py = p + h - y * h
                                                if (i === 0) c.moveTo(px, py); else c.lineTo(px, py)
                                            }
                                            c.stroke()
                                        }
                                    }
                                    Text { text: "Stick → camera speed"; color: Theme.text2; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                                }
                            }
                        }
                    }
                }

                // ---------- assignments ----------
                Card {
                    Layout.fillWidth: true
                    implicitHeight: asCol.implicitHeight + 40
                    ColumnLayout {
                        id: asCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 20
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Lbl { text: "Assignments · " + (root.currentType ? root.currentType.shortName : ""); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            Rectangle {
                                visible: root.notice.length > 0
                                radius: 999
                                implicitHeight: 28
                                implicitWidth: Math.min(600, nt.implicitWidth + 32)
                                color: root.noticeWarn ? Theme.warnSoft : Theme.accentSoft
                                border.width: 1
                                border.color: root.noticeWarn ? Theme.warn : Theme.accent
                                Text { id: nt; anchors.centerIn: parent; width: parent.width - 24; elide: Text.ElideRight; text: root.notice; color: Theme.text; font.pixelSize: 12 }
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 760 ? 2 : 1
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: joystick.actions()
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool listening: root.listenAction === modelData.id
                                    readonly property string bound: joystick.bindings[modelData.id] || ""
                                    Layout.fillWidth: true
                                    implicitHeight: 52
                                    radius: 8
                                    color: listening ? Theme.accentSoft : Theme.s2
                                    border.width: 1
                                    border.color: listening ? Theme.accent : Theme.border
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 8
                                        spacing: 10
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            RowLayout {
                                                spacing: 6
                                                Text { text: modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                                                Text { visible: modelData.locked; text: "(safety, locked)"; color: Theme.text2; font.pixelSize: 11 }
                                            }
                                            Text { text: modelData.axis ? "Axis · continuous speed" : (modelData.hold ? "Button · hold 1 s" : "Button"); color: Theme.text2; font.pixelSize: 11 }
                                        }
                                        Rectangle {
                                            implicitWidth: Math.max(90, chip.implicitWidth + 20)
                                            implicitHeight: 28
                                            radius: 6
                                            color: parent.parent.listening ? Theme.accentSoft : (parent.parent.bound.length ? Theme.surface : "transparent")
                                            border.width: 1
                                            border.color: parent.parent.listening ? Theme.accent : (parent.parent.bound.length ? Theme.border : Theme.warn)
                                            Mono {
                                                id: chip
                                                anchors.centerIn: parent
                                                font.pixelSize: 11
                                                color: parent.parent.parent.bound.length || parent.parent.parent.listening ? Theme.text : Theme.warn
                                                text: parent.parent.parent.listening ? "Listening…" : joystick.label(parent.parent.parent.bound)
                                            }
                                        }
                                        Btn {
                                            compact: true
                                            variant: "ghost"
                                            text: parent.parent.listening ? "Cancel" : "Assign"
                                            onClicked: root.startListen(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text2
                            font.pixelSize: 11
                            lineHeight: 1.3
                            text: "Click Assign, then press a button or move a stick. A button already in use moves to the new action and the old one is left unassigned. Stop all is locked so there is always a guaranteed stop. Each device type keeps its own assignments."
                        }
                    }
                }
            }
        }
    }
}
