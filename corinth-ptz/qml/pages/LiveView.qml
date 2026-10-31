import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Corinth

// Live view: header, camera rail, letterboxed feed + toolbar, control panel, status bar.
Rectangle {
    id: root
    color: Theme.pageBg
    signal openSettings()
    signal openInput()
    signal toggleFullscreen()

    property bool railOpen: width > 1180
    property bool panelOpen: true
    property string tab: "ptz"
    property string ptzMode: "joy"
    property bool crosshair: app.localValue("crossDef", "true") === true || String(app.localValue("crossDef", "true")) === "true"
    property int presetIndex: 0
    property var auxState: ({ stab: true, lock: false, wiper: false, heater: true, ir: false })
    property var snapInfo: null
    property string streamError: ""
    property string notice: ""
    property string utcNow: ""

    readonly property bool compactHeader: width < 1560
    readonly property bool tinyHeader: width < 1240
    readonly property bool linkLost: link.state === "lost"
    readonly property bool linkDegraded: link.state === "degraded"
    readonly property var shownCameras: app.cameras.filter(function (c) { return c.shown || c.id === app.activeId })

    function boolSetting(key, def) { var v = app.localValue(key, def); return v === true || String(v) === "true" }

    function takeSnapshot() {
        snapInfo = capture.snapshot({
            camera: app.activeCamera.name || "camera",
            az: ptz.positionKnown ? ptz.az.toFixed(1) : "",
            el: ptz.positionKnown ? ptz.el.toFixed(1) : "",
            zoom: ptz.zoomKnown ? ptz.zoom.toFixed(1) : "",
            channel: app.channel === 2 ? "Thermal" : "Visible",
            operator: ptz.operatorName,
            vesselPosition: "MAVLink not linked",
            hashFiles: boolSetting("hashOn", true),
            format: String(app.localValue("snapFmt", "PNG")),
            folder: String(app.localValue("snapFolder", ""))
        })
        snapTimer.restart()
    }
    function toggleRecord() {
        if (capture.recording) {
            capture.stopRecording()
            auditLog.add(ptz.operatorName, "Recording stopped", "OK", "ok")
            return
        }
        var seg = parseInt(String(app.localValue("segLen", "10 min")))
        var msg = capture.startRecording(app.recordUrl, app.activeCamera.name || "camera",
                                         String(app.localValue("recFolder", "")), isNaN(seg) ? 10 : seg)
        if (msg.length) { notice = msg; noticeTimer.restart(); auditLog.add(ptz.operatorName, "Recording", "Not started · " + msg, "crit") }
        else auditLog.add(ptz.operatorName, "Recording started", app.effectiveStream === "main" ? "Main stream" : "Sub stream", "ok")
    }
    function stepPreset(d) {
        var n = app.presetNames.length
        if (n === 0) return
        presetIndex = (presetIndex + d + n) % n
        ptz.gotoPreset(presetIndex + 1, app.presetNames[presetIndex])
    }

    Timer { id: snapTimer; interval: 6000; onTriggered: root.snapInfo = null }
    Timer { id: noticeTimer; interval: 7000; onTriggered: root.notice = "" }
    Timer {
        interval: 1000; running: true; repeat: true; triggeredOnStart: true
        onTriggered: root.utcNow = new Date().toISOString().substr(11, 8)
    }

    // Joystick actions the UI owns.
    Connections {
        target: app
        function onUiAction(action) {
            if (action === "snapshot") root.takeSnapshot()
            else if (action === "record") root.toggleRecord()
            else if (action === "channel") app.channel = app.channel === 1 ? 2 : 1
            else if (action === "presetNext") root.stepPreset(1)
            else if (action === "presetPrev") root.stepPreset(-1)
            else if (action === "night") app.theme = (app.theme === "night" ? "dark" : "night")
        }
    }
    Connections {
        target: capture
        function onRecordingError(message) { root.notice = message; noticeTimer.restart() }
    }

    // ---- keyboard: WASD / arrows pan-tilt, Q/E zoom, Space stop, 1-9 presets, N night, T thermal
    Item {
        id: keys
        focus: true
        property int kx: 0
        property int ky: 0
        property int kz: 0
        property bool fast: false
        function apply() { ptz.move(kx * (fast ? 1 : 0.6), ky * (fast ? 1 : 0.6), kz, "keyboard") }
        Keys.onPressed: (e) => {
            if (e.isAutoRepeat) { e.accepted = true; return }
            var k = e.key
            keys.fast = (e.modifiers & Qt.ShiftModifier)
            if (k === Qt.Key_W || k === Qt.Key_Up) keys.ky = 1
            else if (k === Qt.Key_S || k === Qt.Key_Down) keys.ky = -1
            else if (k === Qt.Key_A || k === Qt.Key_Left) keys.kx = -1
            else if (k === Qt.Key_D || k === Qt.Key_Right) keys.kx = 1
            else if (k === Qt.Key_E || k === Qt.Key_Plus || k === Qt.Key_Equal) keys.kz = 1
            else if (k === Qt.Key_Q || k === Qt.Key_Minus) keys.kz = -1
            else if (k === Qt.Key_Space) { ptz.stopAll(); e.accepted = true; return }
            else if (k >= Qt.Key_1 && k <= Qt.Key_9) {
                var i = k - Qt.Key_1
                if (i < app.presetNames.length) { root.presetIndex = i; ptz.gotoPreset(i + 1, app.presetNames[i]) }
                e.accepted = true; return
            }
            else if (k === Qt.Key_N) { app.theme = (app.theme === "night" ? "dark" : "night"); e.accepted = true; return }
            else if (k === Qt.Key_T) { app.channel = app.channel === 1 ? 2 : 1; e.accepted = true; return }
            else return
            keys.apply(); e.accepted = true
        }
        Keys.onReleased: (e) => {
            if (e.isAutoRepeat) { e.accepted = true; return }
            var k = e.key
            if ((k === Qt.Key_W || k === Qt.Key_Up) && keys.ky > 0) keys.ky = 0
            else if ((k === Qt.Key_S || k === Qt.Key_Down) && keys.ky < 0) keys.ky = 0
            else if ((k === Qt.Key_A || k === Qt.Key_Left) && keys.kx < 0) keys.kx = 0
            else if ((k === Qt.Key_D || k === Qt.Key_Right) && keys.kx > 0) keys.kx = 0
            else if (k === Qt.Key_E || k === Qt.Key_Plus || k === Qt.Key_Equal || k === Qt.Key_Q || k === Qt.Key_Minus) keys.kz = 0
            else return
            keys.apply(); e.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ================= HEADER =================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: Theme.pageBg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.pageBorder }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 10

                // brand
                Item {
                    implicitWidth: 26; implicitHeight: 26
                    Rectangle { anchors.centerIn: parent; width: 20; height: 20; radius: 10; color: "transparent"; border.width: 1.6; border.color: Theme.pageAccent }
                    Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: "transparent"; border.width: 1.6; border.color: Theme.pageAccent }
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 1.6; height: 5; color: Theme.pageAccent }
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 21; width: 1.6; height: 5; color: Theme.pageAccent }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; x: 0; width: 5; height: 1.6; color: Theme.pageAccent }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; x: 21; width: 5; height: 1.6; color: Theme.pageAccent }
                }
                ColumnLayout {
                    visible: !root.tinyHeader
                    spacing: 0
                    Lbl { text: "Corinth PTZ"; onPage: true; color: Theme.pageText; font.pixelSize: 16; font.letterSpacing: 3.2 }
                    Lbl { text: "Atlantis USV · Camera control"; onPage: true; font.pixelSize: 11 }
                }
                Item { implicitWidth: 6 }

                ComboBox {
                    id: camBox
                    implicitWidth: root.compactHeader ? 200 : 230
                    implicitHeight: 36
                    model: root.shownCameras
                    textRole: "name"
                    currentIndex: Math.max(0, root.shownCameras.findIndex(function (c) { return c.id === app.activeId }))
                    onActivated: (i) => app.setActive(root.shownCameras[i].id)
                    Accessible.name: "Camera"
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    contentItem: Text {
                        leftPadding: 12
                        text: (app.activeCamera.name || "No camera") + "  ·  " + (app.channel === 2 ? "Thermal" : "Visible")
                        color: Theme.pageText; font: camBox.font
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    indicator: Text { x: camBox.width - width - 10; y: (camBox.height - height) / 2; text: "▾"; color: Theme.pageText2 }
                    background: Rectangle { radius: 8; color: Theme.pageS2; border.width: camBox.visualFocus ? 2 : 1; border.color: camBox.visualFocus ? Theme.pageAccent : Theme.pageBorder }
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: camBox.width
                        contentItem: Text { text: modelData.name + "  ·  " + modelData.proto; color: Theme.text; font: camBox.font }
                        background: Rectangle { color: modelData.id === app.activeId ? Theme.accentSoft : (parent.hovered ? Theme.s2 : "transparent"); radius: 6 }
                    }
                    popup: Popup {
                        y: camBox.height + 4; width: camBox.width; padding: 4
                        implicitHeight: contentItem.implicitHeight + 8
                        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: camBox.popup.visible ? camBox.delegateModel : null }
                        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 10 }
                    }
                }

                Item { Layout.fillWidth: true }

                // joystick chip
                Btn {
                    onPage: true
                    compact: true
                    radiusPx: 15
                    text: (joystick.connected ? "Joystick · " + joystick.deviceName : "No joystick")
                    implicitWidth: Math.min(root.compactHeader ? 170 : 240, contentItem.implicitWidth + 24)
                    variant: joystick.connected ? "toggle" : "soft"
                    on: joystick.connected
                    accentColor: Theme.pageGood
                    onClicked: root.openInput()
                    ToolTip.visible: hovered
                    ToolTip.text: "Joystick setup and button mapping"
                }

                // link chip
                Btn {
                    id: linkChip
                    onPage: true
                    compact: true
                    radiusPx: 15
                    implicitWidth: 150
                    text: (root.linkLost ? "● Link lost" : root.linkDegraded ? "● Degraded" : "● Online") +
                          "   " + (link.hasSample && !root.linkLost ? link.rttMs + " ms" : "—")
                    variant: "toggle"
                    on: true
                    accentColor: root.linkLost ? Theme.pageCrit : root.linkDegraded ? Theme.pageWarn : Theme.pageBorder
                    onClicked: linkPop.open()
                    Accessible.name: "Link health details"
                }

                ColumnLayout {
                    visible: !root.tinyHeader
                    spacing: 0
                    Mono { text: root.utcNow + " Z"; onPage: true; font.pixelSize: 13; Layout.alignment: Qt.AlignRight }
                    Lbl { text: "UTC · system clock"; onPage: true; font.pixelSize: 10; Layout.alignment: Qt.AlignRight }
                }

                // control token
                Rectangle {
                    implicitHeight: 38
                    implicitWidth: ctlRow.implicitWidth + 12
                    radius: 10
                    color: Theme.pageS2
                    border.width: 1
                    border.color: ptz.controlHeld ? Theme.pageGood : Theme.pageBorder
                    RowLayout {
                        id: ctlRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8
                        spacing: 8
                        Rectangle {
                            width: 26; height: 26; radius: 13
                            color: ptz.controlHeld ? Theme.primary : "#353535"
                            Text { anchors.centerIn: parent; text: ptz.operatorName.substr(0, 2).toUpperCase(); color: "#F2EDEA"; font.pixelSize: 11; font.weight: Font.DemiBold }
                        }
                        ColumnLayout {
                            visible: !root.compactHeader
                            spacing: 0
                            Text { text: ptz.operatorName; color: Theme.pageText; font.family: Theme.uiFont; font.pixelSize: 12; font.weight: Font.Medium }
                            Lbl { text: ptz.controlHeld ? "Operator · in control" : "Operator · view only"; onPage: true; font.pixelSize: 10 }
                        }
                        Btn {
                            compact: true
                            onPage: true
                            text: ptz.controlHeld ? "Release" : "Take control"
                            variant: ptz.controlHeld ? "ghost" : "primary"
                            onClicked: ptz.controlHeld ? ptz.releaseControl() : ptz.takeControl()
                        }
                    }
                }

                Seg {
                    onPage: true
                    small: true
                    model: [{ id: "light", label: "Day" }, { id: "dark", label: "Dark" }, { id: "night", label: "Night" }]
                    current: app.theme
                    onPicked: (id) => app.theme = id
                }

                Btn { onPage: true; text: "⚙"; implicitWidth: 36; onClicked: root.openSettings(); Accessible.name: "Settings"; ToolTip.visible: hovered; ToolTip.text: "Settings" }
            }
        }

        // ================= BODY =================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 20

            // ---- camera rail ----
            Card {
                Layout.fillHeight: true
                Layout.preferredWidth: root.railOpen ? 230 : 64
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.railOpen ? 18 : 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Lbl { visible: root.railOpen; text: "Cameras"; color: Theme.text; font.pixelSize: 13 }
                        Text { visible: root.railOpen; text: app.cameras.length + " saved"; color: Theme.text2; font.pixelSize: 11; Layout.fillWidth: true }
                        Btn {
                            text: root.railOpen ? "‹" : "›"
                            variant: root.railOpen ? "ghost" : "soft"
                            implicitWidth: root.railOpen ? 30 : 44
                            implicitHeight: root.railOpen ? 30 : 40
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: root.railOpen = !root.railOpen
                            Accessible.name: root.railOpen ? "Collapse camera list" : "Expand camera list"
                        }
                    }
                    Repeater {
                        model: root.shownCameras
                        delegate: Btn {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.railOpen ? 50 : 44
                            variant: "toggle"
                            on: modelData.id === app.activeId
                            text: ""
                            onClicked: app.setActive(modelData.id)
                            Accessible.name: modelData.name
                            contentItem: RowLayout {
                                spacing: 10
                                Dot { color: modelData.id === app.activeId && root.linkLost ? Theme.text2 : Theme.good; Layout.alignment: Qt.AlignVCenter }
                                ColumnLayout {
                                    visible: root.railOpen
                                    spacing: 2
                                    Layout.fillWidth: true
                                    Text { text: modelData.name; color: Theme.text; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.proto + (modelData.proto === "Demo" ? "" : " · " + modelData.ip); color: Theme.text2; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Mono { visible: !root.railOpen; text: modelData.name.substr(0, 3); font.pixelSize: 11 }
                            }
                        }
                    }
                    Btn {
                        visible: root.railOpen
                        Layout.fillWidth: true
                        variant: "ghost"
                        text: "+  Add camera"
                        onClicked: root.openSettings()
                    }
                    Item { Layout.fillHeight: true }

                    // health
                    ColumnLayout {
                        visible: root.railOpen
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Lbl { text: "Health · " + (app.activeCamera.name || ""); color: Theme.text; Layout.fillWidth: true }
                            Text { text: root.linkLost ? "Stale" : (link.hasSample ? "Live" : "—"); color: Theme.text2; font.pixelSize: 11 }
                        }
                        Repeater {
                            model: [
                                { label: "Link", value: root.linkLost ? "Lost" : (link.hasSample ? link.rttMs + " ms" : "—"), c: root.linkLost ? Theme.crit : root.linkDegraded ? Theme.warn : Theme.good },
                                { label: "Packet loss", value: link.hasSample ? link.lossPct.toFixed(1) + "%" : "—", c: link.lossPct > 3 ? Theme.warn : Theme.good },
                                { label: "PTZ position", value: ptz.positionKnown ? "Reporting" : "No data", c: ptz.positionKnown ? Theme.good : Theme.text2 },
                                { label: "Protocol", value: app.driverKind, c: Theme.text2 },
                                { label: "Recorder", value: capture.recording ? "Recording" : (capture.ffmpegAvailable ? "Ready" : "ffmpeg missing"), c: capture.recording ? Theme.crit : (capture.ffmpegAvailable ? Theme.good : Theme.warn) }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Dot { size: 6; color: modelData.c }
                                Text { text: modelData.label; color: Theme.text2; font.pixelSize: 12; Layout.fillWidth: true }
                                Mono { text: modelData.value }
                            }
                        }
                        Text {
                            text: "Gimbal temperature, heater and storage need the camera's vendor endpoints. Add them from a DevTools capture."
                            color: Theme.text2; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                }
            }

            // ---- feed + toolbar ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Card {
                    id: feed
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 280
                    color: "#000000"
                    clip: true

                    MediaPlayer {
                        id: player
                        source: app.streamUrl
                        videoOutput: video
                        onSourceChanged: {
                            root.streamError = ""
                            if (source.toString().length) play(); else stop()
                        }
                        onErrorOccurred: (error, errorString) => root.streamError = errorString
                        Component.onCompleted: if (source.toString().length) play()
                    }
                    VideoOutput {
                        id: video
                        anchors.fill: parent
                        anchors.margins: 1
                        fillMode: VideoOutput.PreserveAspectFit     // letterbox, never crop
                        Component.onCompleted: capture.videoSink = video.videoSink
                    }

                    // placeholder scene when there is no picture yet
                    Item {
                        anchors.centerIn: parent
                        visible: !capture.hasFrame || app.streamUrl.length === 0
                        width: Math.min(parent.width, parent.height * 16 / 9)
                        height: width * 9 / 16
                        Rectangle { width: parent.width; height: parent.height * 0.52; color: "#3F5A66" }
                        Rectangle { y: parent.height * 0.52; width: parent.width; height: parent.height * 0.48; color: "#16303D" }
                        Rectangle { y: parent.height * 0.52; width: parent.width; height: 2; color: "#6C9BAD" }
                        Repeater {
                            model: [0.62, 0.73, 0.86]
                            Rectangle { required property real modelData; y: parent.height * modelData; width: parent.width; height: 3; color: "#1C3E4E" }
                        }
                        Rectangle { x: parent.width * 0.63; y: parent.height * 0.52 - 12; width: parent.width * 0.075; height: 12; color: "#090909" }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 56
                            radius: 999; color: Theme.overlayBg
                            implicitWidth: phText.implicitWidth + 24; implicitHeight: 28
                            Text {
                                id: phText
                                anchors.centerIn: parent
                                color: Theme.overlayText; font.pixelSize: 12
                                text: app.streamUrl.length === 0
                                      ? (app.driverKind === "Demo" ? "Demo camera · no video source (set a stream address in Settings › Cameras to test video)"
                                                                   : "Waiting for stream address from the camera…")
                                      : (root.streamError.length ? "Stream error · " + root.streamError : "Connecting to stream…")
                            }
                        }
                    }

                    // night tint
                    Rectangle { anchors.fill: parent; visible: Theme.night; color: Qt.rgba(0.35, 0.02, 0.0, 0.28) }

                    // readout
                    Rectangle {
                        x: 14; y: 14; radius: 999
                        color: Theme.overlayBg
                        implicitWidth: ro.implicitWidth + 24; implicitHeight: 28
                        Row {
                            id: ro
                            anchors.centerIn: parent
                            spacing: 14
                            Mono { color: Theme.overlayText; text: "Az " + (ptz.positionKnown ? ptz.az.toFixed(1) + "°" : "—") }
                            Mono { color: Theme.overlayText; text: "El " + (ptz.positionKnown ? ptz.el.toFixed(1) + "°" : "—") }
                            Mono { color: Theme.overlayText; text: "Zoom " + (ptz.zoomKnown ? ptz.zoom.toFixed(1) + "×" : "—") }
                        }
                    }
                    // REC
                    Rectangle {
                        visible: capture.recording
                        anchors.right: parent.right; anchors.rightMargin: 14; y: 14
                        radius: 999; color: Theme.overlayBg
                        implicitWidth: recRow.implicitWidth + 24; implicitHeight: 28
                        Row {
                            id: recRow
                            anchors.centerIn: parent
                            spacing: 8
                            Dot { color: Theme.crit; anchors.verticalCenter: parent.verticalCenter
                                SequentialAnimation on opacity { running: capture.recording; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
                            Mono { color: Theme.overlayText; text: "REC · since " + capture.recordStarted + " Z" }
                        }
                    }
                    // crosshair
                    Item {
                        visible: root.crosshair && !root.linkLost
                        anchors.centerIn: parent
                        width: 56; height: 56
                        Rectangle { x: 27; y: 4; width: 1.5; height: 18; color: Theme.overlayText; opacity: 0.85 }
                        Rectangle { x: 27; y: 34; width: 1.5; height: 18; color: Theme.overlayText; opacity: 0.85 }
                        Rectangle { x: 4; y: 27; width: 18; height: 1.5; color: Theme.overlayText; opacity: 0.85 }
                        Rectangle { x: 34; y: 27; width: 18; height: 1.5; color: Theme.overlayText; opacity: 0.85 }
                    }
                    // banner (view-only or degraded)
                    Rectangle {
                        readonly property string msg: root.linkDegraded ? "Link degraded · sub stream · PTZ speed capped at 30"
                                                    : (!ptz.controlHeld ? "View only · camera controls locked" : "")
                        visible: msg.length > 0 && !root.linkLost
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 56
                        radius: 999
                        color: Theme.overlayBgStrong
                        border.width: 1
                        border.color: root.linkDegraded ? Theme.warn : Theme.accent
                        implicitWidth: bannerRow.implicitWidth + 28; implicitHeight: 30
                        Row {
                            id: bannerRow
                            anchors.centerIn: parent
                            spacing: 8
                            Dot { color: root.linkDegraded ? Theme.warn : Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                            Lbl { text: parent.parent.msg; color: Theme.overlayText }
                        }
                    }
                    // link lost
                    Rectangle {
                        anchors.fill: parent
                        visible: root.linkLost
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            Lbl { anchors.horizontalCenter: parent.horizontalCenter; text: "Link lost"; color: Theme.overlayText; font.pixelSize: 18; font.letterSpacing: 3.6 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Showing last frame · movement stopped · reconnecting"; color: Theme.overlayText2; font.pixelSize: 12 }
                        }
                    }
                    // snapshot card
                    Rectangle {
                        visible: root.snapInfo !== null
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14
                        width: 300
                        implicitHeight: snapCol.implicitHeight + 24
                        radius: 12
                        color: Theme.overlayBgStrong
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            id: snapCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Dot { color: root.snapInfo && root.snapInfo.ok ? Theme.good : Theme.crit }
                                Lbl { text: root.snapInfo && root.snapInfo.ok ? "Snapshot saved" : "Snapshot failed"; color: Theme.overlayText; Layout.fillWidth: true }
                                Mono { text: root.snapInfo ? root.snapInfo.time + " Z" : ""; color: Theme.overlayText2 }
                            }
                            Mono {
                                Layout.fillWidth: true
                                color: Theme.overlayText2
                                lineHeight: 1.4
                                wrapMode: Text.WrapAnywhere
                                text: !root.snapInfo ? "" : (root.snapInfo.ok
                                      ? ("Az " + (ptz.positionKnown ? ptz.az.toFixed(1) : "—") + "° · El " + (ptz.positionKnown ? ptz.el.toFixed(1) : "—") + "° · " + (app.channel === 2 ? "Thermal" : "Visible") +
                                         "\nVessel pos: MAVLink not linked\nOperator: " + ptz.operatorName + (root.snapInfo.hash ? " · SHA-256 " + root.snapInfo.hash : ""))
                                      : root.snapInfo.error)
                            }
                        }
                    }
                    // notice (errors from recording etc.)
                    Rectangle {
                        visible: root.notice.length > 0
                        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 14
                        width: Math.min(parent.width - 340, noticeText.implicitWidth + 28)
                        implicitHeight: noticeText.implicitHeight + 16
                        radius: 10; color: Theme.overlayBgStrong; border.width: 1; border.color: Theme.warn
                        Text { id: noticeText; anchors.centerIn: parent; width: parent.width - 28; text: root.notice; color: Theme.overlayText; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onPressed: (m) => { keys.forceActiveFocus(); m.accepted = false }
                    }
                }

                // toolbar
                Card {
                    Layout.fillWidth: true
                    implicitHeight: 58
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8
                        Btn { text: "Snapshot"; onClicked: root.takeSnapshot() }
                        Btn {
                            text: capture.recording ? "●  Stop recording" : "●  Record"
                            variant: "toggle"; on: capture.recording; accentColor: Theme.crit
                            onClicked: root.toggleRecord()
                        }
                        Rectangle { width: 1; height: 24; color: Theme.border }
                        Seg {
                            opacity: app.streamForced ? 0.6 : 1
                            model: [{ id: "main", label: "Main" }, { id: "sub", label: "Sub" }]
                            current: app.effectiveStream
                            onPicked: (id) => { if (!app.streamForced) app.userStream = id }
                        }
                        Lbl { visible: app.streamForced; text: "Auto · link"; color: Theme.warn; font.pixelSize: 11 }
                        Seg {
                            model: [{ id: "1", label: "Visible" }, { id: "2", label: "Thermal" }]
                            current: String(app.channel)
                            onPicked: (id) => app.channel = parseInt(id)
                        }
                        Item { Layout.fillWidth: true }
                        Btn { text: "✛"; variant: "toggle"; on: root.crosshair; implicitWidth: 36; onClicked: root.crosshair = !root.crosshair; Accessible.name: "Crosshair" }
                        Btn { text: "⛶"; implicitWidth: 36; onClicked: root.toggleFullscreen(); Accessible.name: "Fullscreen" }
                    }
                }
            }

            // ---- control panel ----
            Card {
                Layout.fillHeight: true
                Layout.preferredWidth: root.panelOpen ? 340 : 64
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                clip: true

                // collapsed strip
                ColumnLayout {
                    visible: !root.panelOpen
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Btn { text: "‹"; implicitWidth: 44; implicitHeight: 40; Layout.alignment: Qt.AlignHCenter; onClicked: root.panelOpen = true; Accessible.name: "Expand control panel" }
                    Rectangle { Layout.alignment: Qt.AlignHCenter; width: 28; height: 1; color: Theme.border }
                    Repeater {
                        model: [{ id: "ptz", g: "✥", n: "PTZ" }, { id: "image", g: "☀", n: "Image" }, { id: "presets", g: "⚑", n: "Presets" }, { id: "aux", g: "≡", n: "Aux" }, { id: "log", g: "☰", n: "Log" }]
                        delegate: Btn {
                            required property var modelData
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 44; implicitHeight: 44
                            variant: "ghost"; text: modelData.g
                            onClicked: { root.tab = modelData.id; root.panelOpen = true }
                            Accessible.name: modelData.n
                            ToolTip.visible: hovered; ToolTip.text: modelData.n
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                ColumnLayout {
                    visible: root.panelOpen
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Seg {
                            Layout.fillWidth: true
                            model: [{ id: "ptz", label: "PTZ" }, { id: "image", label: "Image" }, { id: "presets", label: "Presets" }, { id: "aux", label: "Aux" }, { id: "log", label: "Log" }]
                            current: root.tab
                            onPicked: (id) => root.tab = id
                        }
                        Btn { text: "›"; variant: "ghost"; implicitWidth: 34; onClicked: root.panelOpen = false; Accessible.name: "Collapse control panel" }
                    }

                    // last command
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 52
                        radius: 10
                        color: Theme.s2
                        border.width: 1
                        border.color: Theme.border
                        readonly property color stateColor: ptz.lastState === "idle" ? Theme.text2 : ptz.lastState === "ack" ? Theme.good : ptz.lastState === "sending" ? Theme.accent : Theme.crit
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10
                            Dot { color: parent.parent.stateColor }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Lbl { text: "Last command"; font.pixelSize: 10 }
                                Text { text: ptz.lastText; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Lbl {
                                color: parent.parent.stateColor
                                font.pixelSize: 11
                                text: ptz.lastState === "idle" ? "—" : ptz.lastState === "ack" ? "Ack " + ptz.lastMs + " ms"
                                    : ptz.lastState === "sending" ? "Sending…"
                                    : ptz.lastState === "blocked" ? "Not sent" : "Failed"
                                ToolTip.visible: ackHover.hovered && ptz.lastError.length > 0
                                ToolTip.text: ptz.lastError
                                HoverHandler { id: ackHover }
                            }
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: ["ptz", "image", "presets", "aux", "log"].indexOf(root.tab)

                        // ---------- PTZ ----------
                        Flickable {
                            contentHeight: ptzCol.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ColumnLayout {
                                id: ptzCol
                                width: parent.width
                                spacing: 16
                                enabled: !ptz.locked
                                opacity: ptz.locked ? 0.4 : 1
                                Behavior on opacity { NumberAnimation { duration: 160 } }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Lbl { text: "Control mode"; Layout.fillWidth: true }
                                    Seg { small: true; model: [{ id: "pad", label: "Pad" }, { id: "joy", label: "Joystick" }]; current: root.ptzMode; onPicked: (id) => root.ptzMode = id }
                                }
                                DirectionPad {
                                    visible: root.ptzMode === "pad"
                                    Layout.alignment: Qt.AlignHCenter
                                    onPressedDir: (dx, dy) => ptz.move(dx, dy, 0, "pad")
                                    onReleasedDir: ptz.stopMove()
                                    onStopClicked: ptz.stopAll()
                                }
                                ColumnLayout {
                                    visible: root.ptzMode === "joy"
                                    Layout.fillWidth: true
                                    spacing: 10
                                    VirtualJoystick {
                                        id: vjoy
                                        Layout.alignment: Qt.AlignHCenter
                                        onMoved: (x, y) => ptz.move(x, y, 0, "on-screen joystick")
                                        onReleased: ptz.stopMove()
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: [{ n: "Pan", v: vjoy.jx }, { n: "Tilt", v: vjoy.jy }]
                                            Rectangle {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                implicitHeight: 34; radius: 8; color: Theme.s2
                                                RowLayout {
                                                    anchors.fill: parent; anchors.margins: 10
                                                    Lbl { text: modelData.n; font.pixelSize: 11; Layout.fillWidth: true }
                                                    Mono { text: (modelData.v > 0 ? "+" : modelData.v < 0 ? "−" : " ") + Math.abs(Math.round(modelData.v * ptz.effectiveSpeed)) }
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WordWrap
                                        color: Theme.text2; font.pixelSize: 11
                                        text: joystick.connected ? "Joystick connected: " + joystick.label(joystick.bindings.pantilt) + " pans and tilts, " + joystick.label(joystick.bindings.zoom) + " zooms. Release to stop."
                                                                 : "Drag the knob or hold the arrow keys. Release to stop."
                                    }
                                    Btn { text: "Map joystick buttons →"; variant: "ghost"; compact: true; Layout.alignment: Qt.AlignHCenter; onClicked: root.openInput() }
                                }

                                SliderRow {
                                    Layout.fillWidth: true
                                    label: "PTZ speed"
                                    from: 1; to: 100
                                    value: ptz.speed
                                    badge: ptz.speedCap < ptz.speed ? "Capped " + ptz.speedCap : ""
                                    onMoved: (v) => ptz.speed = Math.round(v)
                                }

                                Repeater {
                                    model: [
                                        { name: "Zoom", minus: "Wide", plus: "Tele", kind: "zoom" },
                                        { name: "Focus", minus: "Near", plus: "Far", kind: "focus" },
                                        { name: "Iris", minus: "Close", plus: "Open", kind: "iris" }
                                    ]
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Lbl { text: modelData.name; Layout.fillWidth: true }
                                        Repeater {
                                            model: [-1, 1]
                                            delegate: Btn {
                                                required property int modelData
                                                readonly property var row: parent.modelData
                                                text: modelData < 0 ? row.minus : row.plus
                                                implicitWidth: 64; implicitHeight: 40
                                                font.pixelSize: 12
                                                function stopIt() {
                                                    if (row.kind === "zoom") ptz.stopMove()
                                                    else if (row.kind === "focus") ptz.focus(0)
                                                    else ptz.iris(0)
                                                }
                                                onPressed: {
                                                    if (row.kind === "zoom") ptz.move(0, 0, modelData, "zoom")
                                                    else if (row.kind === "focus") ptz.focus(modelData)
                                                    else ptz.iris(modelData)
                                                }
                                                onReleased: stopIt()
                                                onCanceled: stopIt()
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Btn { text: "One-push focus"; variant: "ghost"; Layout.fillWidth: true; font.pixelSize: 12; onClicked: ptz.onePushFocus() }
                                    Btn { text: "Home"; variant: "ghost"; Layout.fillWidth: true; font.pixelSize: 12; onClicked: ptz.home() }
                                }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    columnSpacing: 8
                                    Repeater {
                                        model: [
                                            { n: "Azimuth", v: ptz.positionKnown ? ptz.az.toFixed(1) + "°" : "—" },
                                            { n: "Elevation", v: ptz.positionKnown ? ptz.el.toFixed(1) + "°" : "—" },
                                            { n: "Zoom", v: ptz.zoomKnown ? ptz.zoom.toFixed(1) + "×" : "—" }
                                        ]
                                        Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            implicitHeight: 56; radius: 8; color: Theme.s2
                                            ColumnLayout {
                                                anchors.fill: parent; anchors.margins: 10; spacing: 2
                                                Lbl { text: modelData.n; font.pixelSize: 11 }
                                                Mono { text: modelData.v; font.pixelSize: 15 }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ---------- IMAGE ----------
                        Flickable {
                            id: imageTab
                            contentHeight: imgCol.implicitHeight
                            clip: true
                            property string colorXml: ""
                            property int bright: 50
                            property int contrast: 50
                            property string dayNight: "Auto"
                            function load() {
                                app.request("GET", "/ISAPI/Image/channels/1/color", "", function (ok, status, body) {
                                    if (!ok) return
                                    imageTab.colorXml = body
                                    var b = /<brightnessLevel>(\d+)</.exec(body); if (b) imageTab.bright = parseInt(b[1])
                                    var c = /<contrastLevel>(\d+)</.exec(body); if (c) imageTab.contrast = parseInt(c[1])
                                })
                            }
                            function push() {
                                if (!colorXml.length) return
                                var x = colorXml.replace(/<brightnessLevel>\d+</, "<brightnessLevel>" + bright + "<")
                                                .replace(/<contrastLevel>\d+</, "<contrastLevel>" + contrast + "<")
                                app.request("PUT", "/ISAPI/Image/channels/1/color", x, function (ok, status, body, ms) {
                                    auditLog.add(ptz.operatorName, "Image · brightness " + imageTab.bright + ", contrast " + imageTab.contrast,
                                                 ok ? "Ack " + ms + " ms" : "Failed · HTTP " + status, ok ? "ok" : "crit")
                                    if (ok) imageTab.colorXml = x
                                })
                            }
                            Timer { id: pushTimer; interval: 400; onTriggered: imageTab.push() }
                            Connections { target: app; function onActiveChanged() { imageTab.load() } }
                            Component.onCompleted: load()

                            ColumnLayout {
                                id: imgCol
                                width: parent.width
                                spacing: 14
                                enabled: !ptz.locked
                                opacity: ptz.locked ? 0.4 : 1
                                SliderRow { Layout.fillWidth: true; label: "Brightness"; value: imageTab.bright; onMoved: (v) => { imageTab.bright = Math.round(v); pushTimer.restart() } }
                                SliderRow { Layout.fillWidth: true; label: "Contrast"; value: imageTab.contrast; onMoved: (v) => { imageTab.contrast = Math.round(v); pushTimer.restart() } }
                                DropField {
                                    Layout.fillWidth: true
                                    label: "Day / night"
                                    model: ["Auto", "Day", "Night"]
                                    value: imageTab.dayNight
                                    onPicked: (v) => {
                                        imageTab.dayNight = v
                                        var map = { "Auto": "auto", "Day": "day", "Night": "night" }
                                        app.request("PUT", "/ISAPI/Image/channels/1/IrcutFilter",
                                                    "<IrcutFilter><IrcutFilterType>" + map[v] + "</IrcutFilterType></IrcutFilter>",
                                                    function (ok, status, body, ms) {
                                                        auditLog.add(ptz.operatorName, "Day/night · " + v, ok ? "Ack " + ms + " ms" : "Failed · HTTP " + status, ok ? "ok" : "crit")
                                                    })
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: "Full image settings live under Settings › Image. Thermal palette needs the camera's vendor endpoint."
                                    color: Theme.text2; font.pixelSize: 11
                                }
                            }
                        }

                        // ---------- PRESETS ----------
                        Flickable {
                            contentHeight: preCol.implicitHeight
                            clip: true
                            ColumnLayout {
                                id: preCol
                                width: parent.width
                                spacing: 8
                                enabled: !ptz.locked
                                opacity: ptz.locked ? 0.4 : 1
                                Repeater {
                                    model: app.presetNames
                                    delegate: Rectangle {
                                        required property string modelData
                                        required property int index
                                        Layout.fillWidth: true
                                        implicitHeight: 48
                                        radius: 8
                                        color: root.presetIndex === index ? Theme.accentSoft : Theme.s2
                                        border.width: 1
                                        border.color: root.presetIndex === index ? Theme.accent : Theme.border
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 8
                                            spacing: 10
                                            Mono { text: index + 1; color: Theme.text2; Layout.preferredWidth: 20 }
                                            Text { text: modelData; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Btn { text: "Go"; variant: "ghost"; compact: true; onClicked: { root.presetIndex = index; ptz.gotoPreset(index + 1, modelData) } }
                                        }
                                        TapHandler { onTapped: root.presetIndex = index }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Btn { text: "Set current"; variant: "ghost"; Layout.fillWidth: true; onClicked: ptz.setPreset(root.presetIndex + 1, app.presetNames[root.presetIndex]) }
                                    Btn {
                                        text: "Rename"
                                        variant: "ghost"
                                        Layout.fillWidth: true
                                        onClicked: { renameField.text = app.presetNames[root.presetIndex]; renamePop.open() }
                                    }
                                }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                                    text: "Select a row, then Set current to store the camera's present position under that number." }
                            }
                            Popup {
                                id: renamePop
                                modal: true
                                parent: root
                                anchors.centerIn: parent
                                padding: 16
                                background: Card {}
                                contentItem: ColumnLayout {
                                    spacing: 10
                                    Lbl { text: "Rename preset " + (root.presetIndex + 1); color: Theme.text }
                                    TextField {
                                        id: renameField
                                        implicitWidth: 260
                                        color: Theme.text
                                        background: Rectangle { radius: 8; color: Theme.s2; border.color: Theme.border }
                                    }
                                    RowLayout {
                                        Btn { text: "Cancel"; variant: "ghost"; onClicked: renamePop.close() }
                                        Btn { text: "Save"; variant: "primary"; onClicked: { app.setPresetName(root.presetIndex, renameField.text); renamePop.close() } }
                                    }
                                }
                            }
                        }

                        // ---------- AUX ----------
                        Flickable {
                            contentHeight: auxCol.implicitHeight
                            clip: true
                            ColumnLayout {
                                id: auxCol
                                width: parent.width
                                spacing: 8
                                enabled: !ptz.locked
                                opacity: ptz.locked ? 0.4 : 1
                                Repeater {
                                    model: [
                                        { id: "stab", label: "Stabilisation" }, { id: "lock", label: "Heading lock" },
                                        { id: "wiper", label: "Wiper" }, { id: "heater", label: "Heater / defog" }, { id: "ir", label: "IR light" }
                                    ]
                                    delegate: ToggleRow {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        label: modelData.label
                                        isOn: !!root.auxState[modelData.id]
                                        onToggledByUser: (v) => {
                                            var s = Object.assign({}, root.auxState); s[modelData.id] = v; root.auxState = s
                                            ptz.aux(modelData.id, v, modelData.label)
                                        }
                                    }
                                }
                                Btn { text: "Return gimbal to centre"; variant: "ghost"; Layout.fillWidth: true; onClicked: ptz.home() }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                                    text: "Wiper and IR light use standard ISAPI/ONVIF aux commands. Stabilisation, heading lock and heater are vendor-specific: the last command bar shows if the camera rejects them." }
                            }
                        }

                        // ---------- LOG ----------
                        ColumnLayout {
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Lbl { text: "Audit log · this session"; Layout.fillWidth: true }
                                Text { text: auditLog.count + " entries"; color: Theme.text2; font.pixelSize: 11 }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 4
                                model: auditLog
                                delegate: Rectangle {
                                    required property string time
                                    required property string who
                                    required property string what
                                    required property string result
                                    required property string kind
                                    width: ListView.view.width
                                    implicitHeight: logGrid.implicitHeight + 16
                                    radius: 8
                                    color: Theme.s2
                                    GridLayout {
                                        id: logGrid
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        columns: 2
                                        columnSpacing: 10
                                        rowSpacing: 2
                                        Mono { text: time; color: Theme.text2; Layout.preferredWidth: 60 }
                                        Text { text: what; color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { text: who; color: Theme.text2; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 60 }
                                        Mono { text: result; font.pixelSize: 11; color: kind === "crit" ? Theme.crit : kind === "warn" ? Theme.warn : Theme.good; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                }
                            }
                            Btn {
                                Layout.fillWidth: true
                                variant: "ghost"
                                text: "Export log (CSV)"
                                onClicked: { var p = auditLog.exportCsv(); root.notice = p.length ? "Log exported to " + p : "Could not write the export file"; noticeTimer.restart() }
                            }
                            Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.text2; font.pixelSize: 11; text: "Daily log files: " + auditLog.logFolder }
                        }
                    }

                    // shortcuts
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        Repeater {
                            model: [["WASD", "pan/tilt"], ["Q E", "zoom"], ["Space", "stop"], ["1–9", "preset"], ["T", "thermal"], ["N", "night"]]
                            Row {
                                required property var modelData
                                spacing: 4
                                Mono { text: modelData[0]; font.pixelSize: 11 }
                                Text { text: modelData[1]; color: Theme.text2; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }
        }

        // ================= FOOTER =================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.pageBg
            Rectangle { width: parent.width; height: 1; color: Theme.pageBorder }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 20
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Stream: " + (app.effectiveStream === "main" ? "main" : "sub") + (app.streamForced ? " · auto" : "") }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "RTT " + (link.hasSample && !root.linkLost ? link.rttMs + " ms" : "—") }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Jitter " + (link.hasSample ? link.jitterMs + " ms" : "—") }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Loss " + (link.hasSample ? link.lossPct.toFixed(1) + "%" : "—") }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Protocol " + app.driverKind }
                Item { Layout.fillWidth: true }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Control: " + (ptz.controlHeld ? ptz.operatorName + " (operator)" : "released") }
                Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Heading — (MAVLink not linked)" }
            }
        }
    }

    // ================= LINK POPOVER =================
    Popup {
        id: linkPop
        parent: linkChip
        y: linkChip.height + 8
        x: linkChip.width - width
        width: 360
        padding: 16
        background: Card {}
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Link health"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Lbl { text: root.linkLost ? "Link lost" : root.linkDegraded ? "Degraded" : "Online"; color: root.linkLost ? Theme.crit : root.linkDegraded ? Theme.warn : Theme.good; font.pixelSize: 11 }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: [
                        { n: "RTT", v: link.hasSample && !root.linkLost ? link.rttMs + " ms" : "—" },
                        { n: "Jitter", v: link.hasSample ? link.jitterMs + " ms" : "—" },
                        { n: "Loss", v: link.hasSample ? link.lossPct.toFixed(1) + "%" : "—" },
                        { n: "Stream", v: app.effectiveStream === "main" ? "Main" : "Sub" },
                        { n: "PTZ cap", v: ptz.speedCap < 100 ? String(ptz.speedCap) : "None" },
                        { n: "Protocol", v: app.driverKind }
                    ]
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 50; radius: 8; color: Theme.s2
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                            Lbl { text: modelData.n; font.pixelSize: 11 }
                            Mono { text: modelData.v; font.pixelSize: 13 } }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text2
                font.pixelSize: 12
                lineHeight: 1.3
                text: "Probed once a second. Over 500 ms RTT or 3% loss, the app switches to the sub stream and caps PTZ speed at 30. After 5 s with no reply, movement stops and controls lock."
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Simulate"; Layout.fillWidth: true; font.pixelSize: 11 }
                Seg {
                    small: true
                    model: [{ id: "", label: "Live" }, { id: "degraded", label: "Degraded" }, { id: "lost", label: "Lost" }]
                    current: link.simulate
                    onPicked: (id) => link.simulate = id
                }
            }
        }
    }
}
