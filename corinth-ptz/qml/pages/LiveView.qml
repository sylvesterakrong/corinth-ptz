import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Corinth

// Live view: header, camera rail, letterboxed feed + toolbar, control panel, status bar.
Rectangle {
    id: root
    color: "#000000"
    signal openSettings()
    signal openInput()
    signal toggleFullscreen()

    property bool railOpen: false
    property bool panelOpen: true
    property string tab: "ptz"
    property string ptzMode: "joy"
    property bool crosshair: app.localValue("crossDef", "true") === true || String(app.localValue("crossDef", "true")) === "true"
    property int presetIndex: 0
    property bool pointMode: false
    property bool renameAdd: false
    property var snapInfo: null
    property string streamError: ""
    property string notice: ""
    property string utcNow: ""

    readonly property bool compactHeader: width < 1560
    readonly property bool tinyHeader: width < 1240
    readonly property bool linkLost: link.state === "lost"
    readonly property bool linkDegraded: link.state === "degraded"
    // ---- who is in control (this laptop only, or shared between laptops)
    // The CJ3000 has no lamp and an optional wiper (spec sheet): its Aux switches are labelled so.
    readonly property bool isCj3000: (app.activeCamera.name || "").toUpperCase().indexOf("CJ3000") >= 0
    readonly property bool teamControl: control.enabled
    readonly property string controlText: !teamControl ? (ptz.controlHeld ? "Operator · in control" : "Operator · view only")
        : control.state === "mine" ? "In control"
        : control.state === "listening" ? "Checking for other operators…"
        : control.state === "other" ? control.holderName + " in control"
        : control.state === "requesting" ? (control.requestTimedOut ? "No answer from " + control.holderName : "Waiting for " + control.holderName + "…")
        : "Nobody in control"
    readonly property string controlButtonText: !teamControl ? (ptz.controlHeld ? "Release" : "Take control")
        : control.state === "mine" ? "Release"
        : control.state === "requesting" ? "Cancel"
        : control.state === "other" ? "Request control" : "Take control"
    readonly property string viewOnlyText: !teamControl ? "View only · camera controls locked"
        : control.state === "other" ? "View only · " + control.holderName + " is in control"
        : control.state === "listening" ? "Checking for other operators…"
        : control.state === "requesting" ? "Waiting for " + control.holderName + " to answer…"
        : "View only · nobody in control"
    function controlAction() {
        if (!teamControl) { ptz.controlHeld ? ptz.releaseControl() : ptz.takeControl(); return }
        if (control.state === "mine") ptz.releaseControl()
        else if (control.state === "requesting") control.cancelRequest()
        else ptz.takeControl()
    }

    // ---- health rows: this laptop's view of the link, plus what the camera reports
    function levelColor(level) { return level === "crit" ? Theme.crit : level === "warn" ? Theme.warn : level === "ok" ? Theme.good : Theme.text2 }
    readonly property var appHealth: [
        { label: "Link", value: linkLost ? "Lost" : (link.hasSample ? link.rttMs + " ms" : "—"), c: linkLost ? Theme.crit : linkDegraded ? Theme.warn : Theme.good },
        { label: "Packet loss", value: link.hasSample ? link.lossPct.toFixed(1) + "%" : "—", c: link.lossPct > 3 ? Theme.warn : Theme.good },
        { label: "PTZ position", value: ptz.positionKnown ? "Reporting" : "No data", c: ptz.positionKnown ? Theme.good : Theme.text2 },
        { label: "Protocol", value: app.driverKind, c: Theme.text2 },
        { label: "Recorder", value: capture.recording ? "Recording" : (capture.ffmpegAvailable ? "Ready" : "ffmpeg missing"), c: capture.recording ? Theme.crit : (capture.ffmpegAvailable ? Theme.good : Theme.warn) }
    ]
    readonly property var cameraHealth: health.items.map(function (it) { return { label: it.label, value: it.text, c: levelColor(it.level) } })

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
        var n = app.presets.length
        if (n === 0) return
        presetIndex = (presetIndex + d + n) % n
        var p = app.presets[presetIndex]
        ptz.gotoPreset(p.id, p.name)
    }
    function curPreset() { return app.presets.length ? app.presets[Math.min(presetIndex, app.presets.length - 1)] : null }
    Connections {
        target: app
        function onPresetsChanged() { if (root.presetIndex >= app.presets.length) root.presetIndex = Math.max(0, app.presets.length - 1) }
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
            root.idle = false; idleTimer.restart()
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
                if (i < app.presets.length) { root.presetIndex = i; ptz.gotoPreset(app.presets[i].id, app.presets[i].name) }
                e.accepted = true; return
            }
            else if (k === Qt.Key_N) { app.theme = (app.theme === "night" ? "dark" : "night"); e.accepted = true; return }
            else if (k === Qt.Key_T) { if (app.hasChannel2) app.channel = app.channel === 1 ? 2 : 1; e.accepted = true; return }
            else if (k === Qt.Key_H) { root.overlays = !root.overlays; e.accepted = true; return }
            else if (k === Qt.Key_P) { if (ptz.canPoint) root.pointMode = !root.pointMode; e.accepted = true; return }
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

    // ================= LAYOUT: the picture fills the window; everything else floats over it =================
    property bool overlays: true       // H key / toolbar button
    property bool idle: false          // hidden by "hide after 8 s without mouse movement"
    property bool isFullScreen: false  // set by the window; in full screen nothing but the picture is shown
    property bool exitChipShown: false
    readonly property bool ov: overlays && !idle && !isFullScreen
    property bool autoHide: boolSetting("autoHide", false)
    property bool blurOverlays: boolSetting("overlayBlur", false)
    readonly property int topBarH: 64
    readonly property real railW: railOpen ? 230 : 64
    readonly property real panelW: panelOpen ? 340 : 64
    readonly property real topInset: topBarH + 12                      // below the top bar
    readonly property real overlayBottom: height - 30 - 12             // above the status strip
    readonly property real gapLeft: 16 + railW + 12                    // the free middle between the side panels
    readonly property real gapRight: width - 16 - panelW - 12
    // Where the chips drawn on the picture (readout, REC, banners ...) may sit without being covered.
    readonly property real contentLeft: ov ? gapLeft : 14
    readonly property real contentRight: ov ? width - gapRight : 14
    readonly property real contentTop: ov ? topInset : 14
    readonly property real contentBottom: ov ? (height - toolbar.y) + 12 : 14

    // Below 1280 px wide the two side panels do not both fit: opening one closes the other.
    onRailOpenChanged: if (width < 1280 && railOpen) panelOpen = false
    onPanelOpenChanged: if (width < 1280 && panelOpen) railOpen = false
    onWidthChanged: if (width < 1280 && railOpen && panelOpen) railOpen = false

    Connections {
        target: app
        function onLocalValueChanged(key) {
            if (key === "autoHide") { root.autoHide = root.boolSetting("autoHide", false); root.idle = false }
            else if (key === "overlayBlur") root.blurOverlays = root.boolSetting("overlayBlur", false)
        }
    }
    // Hide the controls after 8 s of no mouse movement (off by default); any movement or key brings them back.
    Timer { id: idleTimer; interval: 8000; running: root.autoHide && root.overlays && !root.idle; onTriggered: root.idle = true }
    HoverHandler {
        onPointChanged: if (root.autoHide) { root.idle = false; idleTimer.restart() }
    }

    // Full screen, like a browser: move the mouse to the top edge and a small X drops down to leave it.
    HoverHandler {
        onPointChanged: {
            if (!root.isFullScreen) return
            if (point.position.y <= 6) { root.exitChipShown = true; exitChipTimer.restart() }
            else if (root.exitChipShown && point.position.y < 72) exitChipTimer.restart()
        }
    }
    Timer { id: exitChipTimer; interval: 1800; onTriggered: root.exitChipShown = false }
    onIsFullScreenChanged: exitChipShown = false
    Rectangle {
        id: exitChip
        z: 30
        visible: root.isFullScreen && y > -height
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.isFullScreen && root.exitChipShown ? -14 : -height     // the top corners stay off screen
        Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        width: exitRow.implicitWidth + 36
        height: 48
        radius: 16
        color: Theme.overlayBgStrong
        border.width: 1
        border.color: Theme.border
        HoverHandler { id: exitHover; onHoveredChanged: if (hovered) exitChipTimer.stop(); else exitChipTimer.restart() }
        Row {
            id: exitRow
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            spacing: 10
            Text { text: "✕"; color: Theme.overlayText; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Exit full screen"; color: Theme.overlayText2; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { onTapped: root.toggleFullscreen() }
        Accessible.name: "Exit full screen"
        Accessible.role: Accessible.Button
    }

    // The way back when the controls are hidden by hand.
    Rectangle {
        visible: !root.overlays && !root.isFullScreen
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 10
        radius: 999; color: Theme.overlayBgStrong; border.width: 1; border.color: Theme.border
        opacity: restoreHover.hovered ? 1 : 0.55
        implicitWidth: restoreText.implicitWidth + 28; implicitHeight: 28
        Text { id: restoreText; anchors.centerIn: parent; color: Theme.overlayText; font.pixelSize: 12; text: "Controls hidden · press H or click" }
        HoverHandler { id: restoreHover }
        TapHandler { onTapped: root.overlays = true }
    }

    // ================= THE PICTURE =================
    Item {
        id: feed
        anchors.fill: parent
        clip: true

        // The low-latency decoder when this build has it; Qt's MediaPlayer (more delay) when not.
        LowLatencyPlayer {
            id: llPlayer
            source: app.hasLowLatencyPlayer ? app.streamUrl : ""
            videoSink: video.videoSink
            latencyMode: app.latencyMode
            transport: app.transport
            onSourceChanged: root.streamError = ""
            onErrorChanged: if (error.length) root.streamError = error
        }
        MediaPlayer {
            id: player
            source: app.hasLowLatencyPlayer ? "" : app.streamUrl
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
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: parent.height * 0.18   // clear of the toolbar, whatever the window size
                radius: 999; color: Theme.overlayBg
                implicitWidth: phText.implicitWidth + 24; implicitHeight: 28
                Text {
                    id: phText
                    anchors.centerIn: parent
                    color: Theme.overlayText; font.pixelSize: 12
                    text: app.streamUrl.length === 0
                          ? (app.driverKind === "Demo" ? "Demo camera · no video source (set a stream address in Settings › Cameras to test video)"
                                                       : "Waiting for stream address from the camera…")
                          : (app.hasLowLatencyPlayer && llPlayer.state === "reconnecting" ? "Reconnecting…" + (root.streamError.length ? " · " + root.streamError : "")
                             : root.streamError.length ? "Stream error · " + root.streamError : "Connecting to stream…")
                }
            }
        }

        // Night mode restyles the interface only: the camera picture is never recoloured.

        // readout
        Rectangle {
            x: root.contentLeft; y: root.contentTop; radius: 999
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
            anchors.right: parent.right; anchors.rightMargin: root.contentRight; y: root.contentTop
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
        // reconnecting after a drop: say so over the last picture
        Rectangle {
            visible: app.hasLowLatencyPlayer && llPlayer.state === "reconnecting" && capture.hasFrame && !root.linkLost
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            radius: 999; color: Theme.overlayBgStrong; border.width: 1; border.color: Theme.warn
            implicitWidth: reconText.implicitWidth + 28; implicitHeight: 30
            Text { id: reconText; anchors.centerIn: parent; color: Theme.overlayText; font.pixelSize: 12; text: "Reconnecting…" }
        }

        // picture-in-picture: the other channel, always the sub stream, off on a weak link
        Rectangle {
            id: pip
            visible: app.pipActive
            z: 5
            anchors.right: parent.right
            anchors.rightMargin: root.contentRight
            y: root.contentTop + (capture.recording ? 38 : 0)
            width: Math.min(280, parent.width * 0.28)
            height: width * 9 / 16
            color: "#000000"
            radius: 8
            clip: true
            border.width: 1
            border.color: Theme.border
            LowLatencyPlayer {
                source: app.hasLowLatencyPlayer && app.pipActive ? app.pipUrl : ""
                videoSink: pipVideo.videoSink
                transport: app.transport
            }
            MediaPlayer {
                id: pipPlayer
                source: !app.hasLowLatencyPlayer && app.pipActive ? app.pipUrl : ""
                videoOutput: pipVideo
                onSourceChanged: { if (source.toString().length) play(); else stop() }
                Component.onCompleted: if (source.toString().length) play()
            }
            VideoOutput { id: pipVideo; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
            Rectangle {
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 6
                radius: 999; color: Theme.overlayBg
                implicitWidth: pipLabel.implicitWidth + 16; implicitHeight: 20
                Text { id: pipLabel; anchors.centerIn: parent; color: Theme.overlayText; font.pixelSize: 11; text: (app.pipChannel === 2 ? "Thermal" : "Visible") + " · click to swap" }
            }
            TapHandler { onTapped: app.channel = app.pipChannel }
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
                                        : (!ptz.controlHeld ? root.viewOnlyText : "")
            visible: msg.length > 0 && !root.linkLost
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.contentTop
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
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: root.contentRight; anchors.bottomMargin: root.contentBottom
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
                Btn { visible: root.snapInfo && root.snapInfo.ok; text: "Open folder"; compact: true; variant: "ghost"; onClicked: app.openFolder("snapshots") }
            }
        }
        // notice (errors from recording etc.)
        Rectangle {
            visible: root.notice.length > 0
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.leftMargin: root.contentLeft; anchors.bottomMargin: root.contentBottom
            width: Math.min(parent.width - root.contentLeft - root.contentRight - 40, noticeText.implicitWidth + 28)
            implicitHeight: noticeText.implicitHeight + 16
            radius: 10; color: Theme.overlayBgStrong; border.width: 1; border.color: Theme.warn
            Text { id: noticeText; anchors.centerIn: parent; width: parent.width - 28; text: root.notice; color: Theme.overlayText; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }

        // click the picture to centre the camera there; drag a box to zoom into it
        Rectangle {
            visible: root.pointMode && ptz.canPoint
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: root.contentBottom
            radius: 999; color: Theme.overlayBgStrong; border.width: 1; border.color: Theme.accent
            implicitWidth: pointHint.implicitWidth + 28; implicitHeight: 28
            Text { id: pointHint; anchors.centerIn: parent; color: Theme.overlayText; font.pixelSize: 12
                   text: ptz.locked ? "Point mode · locked while view-only" : (ptz.canBoxZoom ? "Point mode · click to centre · drag a box to zoom" : "Point mode · click to centre") }
        }
        MouseArea {
            id: pointArea
            anchors.fill: parent
            enabled: root.pointMode && ptz.canPoint && !ptz.locked && !root.linkLost
            visible: enabled
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.CrossCursor
            property real sx: 0
            property real sy: 0
            property real cx: 0
            property real cy: 0
            property bool dragging: false
            // 0..1 inside the picture itself (the video is letterboxed, so the picture is smaller than the area)
            function norm(mx, my) {
                var q = video.mapFromItem(pointArea, mx, my)
                var r = video.contentRect
                return { x: (q.x - r.x) / r.width, y: (q.y - r.y) / r.height }
            }
            function inside(n) { return n.x >= 0 && n.x <= 1 && n.y >= 0 && n.y <= 1 }
            onPressed: (m) => { keys.forceActiveFocus(); sx = m.x; sy = m.y; cx = m.x; cy = m.y; dragging = false }
            onPositionChanged: (m) => { cx = m.x; cy = m.y; if (Math.abs(cx - sx) > 8 || Math.abs(cy - sy) > 8) dragging = true }
            onReleased: (m) => {
                var a = norm(sx, sy), b = norm(m.x, m.y)
                var was = dragging
                dragging = false
                if (!inside(a) || (was && !inside(b))) return
                if (was && ptz.canBoxZoom) ptz.boxZoom(a.x, a.y, b.x, b.y)
                else if (was) ptz.pointAt((a.x + b.x) / 2, (a.y + b.y) / 2)
                else ptz.pointAt(a.x, a.y)
                ring.x = sx - ring.width / 2; ring.y = sy - ring.height / 2
                ringAnim.restart()
            }
            Rectangle {
                visible: pointArea.dragging
                x: Math.min(pointArea.sx, pointArea.cx); y: Math.min(pointArea.sy, pointArea.cy)
                width: Math.abs(pointArea.cx - pointArea.sx); height: Math.abs(pointArea.cy - pointArea.sy)
                color: Qt.rgba(0.42, 0.61, 0.68, 0.18); border.width: 1.5; border.color: Theme.accent
            }
            Rectangle {
                id: ring
                width: 40; height: 40; radius: 20
                color: "transparent"; border.width: 2; border.color: Theme.accent
                opacity: 0
                ParallelAnimation {
                    id: ringAnim
                    NumberAnimation { target: ring; property: "opacity"; from: 1; to: 0; duration: 700 }
                    NumberAnimation { target: ring; property: "scale"; from: 0.4; to: 1.5; duration: 700; easing.type: Easing.OutCubic }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: (m) => { keys.forceActiveFocus(); m.accepted = false }
        }
    }

    // ================= FLOATING CONTROLS =================
    // ================= HEADER =================
    OverlayCard {
        id: topBar
        visible: root.ov
        x: 0; y: 0                      // docked to the top edge, full width, like v1
        width: root.width
        height: root.topBarH
        radius: 0
        border.width: 0
        color: Qt.rgba(Theme.pageBg.r, Theme.pageBg.g, Theme.pageBg.b, 0.96)   // the page colour: light in Day, as in v1
        blurSource: feed; blur: root.blurOverlays
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
                        Lbl { text: root.controlText; onPage: true; font.pixelSize: 10 }
                    }
                    Btn {
                        compact: true
                        onPage: true
                        text: root.controlButtonText
                        variant: ptz.controlHeld ? "ghost" : "primary"
                        onClicked: root.controlAction()
                    }
                    Btn {
                        visible: root.teamControl && control.canTakeOver
                        compact: true
                        onPage: true
                        text: "Take over…"
                        variant: "ghost"
                        onClicked: takeOverPop.open()
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

    // ---- camera rail ----
    OverlayCard {
        id: rail
        visible: root.ov
        x: 16; y: root.topInset
        width: root.railW
        height: root.overlayBottom - y
        blurSource: feed; blur: root.blurOverlays
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
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
                    Text { text: root.linkLost || health.stale ? "Stale" : (link.hasSample ? "Live" : "—"); color: Theme.text2; font.pixelSize: 11 }
                }
                Repeater {
                    model: root.appHealth.concat(root.cameraHealth)
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
                    visible: !health.available
                    text: "No camera-side readings for this protocol. Add health items to the camera profile (Settings › Endpoint tester)."
                    color: Theme.text2; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
        }
    }

    // toolbar
    OverlayCard {
        id: toolbar
        visible: root.ov
        height: 58
        radius: 29
        width: Math.min(toolRow.implicitWidth + 24, root.gapRight - root.gapLeft)
        x: root.gapLeft + (root.gapRight - root.gapLeft - width) / 2
        y: root.overlayBottom - height
        blurSource: feed; blur: root.blurOverlays
        RowLayout {
            id: toolRow
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 14; anchors.rightMargin: 14
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
                visible: app.hasChannel2
                model: [{ id: "1", label: "Visible" }, { id: "2", label: "Thermal" }]
                current: String(app.channel)
                onPicked: (id) => app.channel = parseInt(id)
            }
            Btn {
                visible: app.hasChannel2
                text: "PiP"
                variant: "toggle"; on: app.pipEnabled
                onClicked: app.pipEnabled = !app.pipEnabled
                Accessible.name: "Picture in picture"
                ToolTip.visible: hovered
                ToolTip.text: "Show the other channel in a small window (sub stream; switches off while the link is degraded)"
            }
            Btn {
                text: "⌖  Point"
                variant: "toggle"; on: root.pointMode
                enabled: ptz.canPoint
                onClicked: root.pointMode = !root.pointMode
                Accessible.name: "Click to point"
                ToolTip.visible: hovered
                ToolTip.text: ptz.canPoint ? "Click the picture to centre the camera there; drag a box to zoom into it (P)" : "This camera protocol cannot point from a click"
            }
            Rectangle { width: 1; height: 24; color: Theme.border }
            Btn { text: "✛"; variant: "toggle"; on: root.crosshair; implicitWidth: 36; onClicked: root.crosshair = !root.crosshair; Accessible.name: "Crosshair" }
            Btn { text: "◨"; implicitWidth: 36; onClicked: root.overlays = false; Accessible.name: "Hide controls"; ToolTip.visible: hovered; ToolTip.text: "Hide the controls (H)" }
            Btn { text: "⛶"; implicitWidth: 36; onClicked: root.toggleFullscreen(); Accessible.name: "Fullscreen" }
        }
    }

    // ---- control panel ----
    OverlayCard {
        id: panelCard
        visible: root.ov
        x: root.width - width - 16; y: root.topInset
        width: root.panelW
        height: root.overlayBottom - y
        blurSource: feed; blur: root.blurOverlays
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
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
                model: [{ id: "ptz", g: "✥", n: "PTZ" }, { id: "image", g: "☀", n: "Image" }, { id: "presets", g: "⚑", n: "Presets" }, { id: "aux", g: "≡", n: "Aux" }, { id: "health", g: "♥", n: "Health" }, { id: "log", g: "☰", n: "Log" }]
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
                    model: [{ id: "ptz", label: "PTZ" }, { id: "image", label: "Image" }, { id: "presets", label: "Presets" }, { id: "aux", label: "Aux" }, { id: "health", label: "Health" }, { id: "log", label: "Log" }]
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
                currentIndex: ["ptz", "image", "presets", "aux", "health", "log"].indexOf(root.tab)

                // ---------- PTZ ----------
                Flickable {
                    contentHeight: ptzCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
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
                                text: joystick.connected ? "Joystick connected: " + joystick.label(joystick.bindings.pantilt) + " pans and tilts, " + joystick.label(joystick.bindings.zoom) + " zooms (the Zoom speed slider sets how fast). Release to stop."
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

                        SliderRow {
                            Layout.fillWidth: true
                            label: "Zoom speed"
                            from: 1; to: 100
                            value: ptz.zoomSpeed
                            badge: ptz.speedCap < ptz.zoomSpeed ? "Capped " + ptz.speedCap : ""
                            onMoved: (v) => ptz.zoomSpeed = Math.round(v)
                        }

                        // Sets the zoom level itself, 1x to 40x. It follows the camera's own zoom, except while it is being dragged.
                        SliderRow {
                            id: zoomLevelRow
                            Layout.fillWidth: true
                            label: "Zoom level"
                            from: 1; to: 40
                            unit: "×"
                            enabled: ptz.canZoomLevel
                            opacity: ptz.canZoomLevel ? 1 : 0.5
                            badge: ptz.canZoomLevel ? "" : "Not available"
                            property bool dragging: false
                            property real pending: 1
                            value: dragging ? pending : (ptz.zoomKnown ? ptz.zoom : pending)
                            onMoved: (v) => { dragging = true; pending = v; zoomSend.restart(); dragHold.restart() }
                            Timer { id: zoomSend; interval: 120; onTriggered: ptz.setZoomLevel(zoomLevelRow.pending) }
                            Timer { id: dragHold; interval: 1500; onTriggered: zoomLevelRow.dragging = false }
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
                        app.request("GET", app.profilePath("image.color"), "", function (ok, status, body) {
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
                        app.request("PUT", app.profilePath("image.color"), x, function (ok, status, body, ms) {
                            auditLog.add(ptz.operatorName, "Image · brightness " + imageTab.bright + ", contrast " + imageTab.contrast,
                                         ok ? "Ack " + ms + " ms" : "Failed · HTTP " + status, ok ? "ok" : "crit")
                            if (ok) imageTab.colorXml = x
                        })
                    }
                    Timer { id: pushTimer; interval: 400; onTriggered: imageTab.push() }
                    Connections { target: app; function onActiveChanged() { imageTab.load() } }
                    Connections { target: app; function onChannelChanged() { imageTab.load() } }
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
                                app.request("PUT", app.profilePath("image.ircut"),
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
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Dot { size: 6; color: app.presetsFromCamera ? Theme.good : Theme.warn }
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                color: Theme.text2; font.pixelSize: 11
                                text: app.presetsFromCamera ? "List read from the camera" : "Names saved on this laptop; the camera's list was not read"
                            }
                            Btn { compact: true; variant: "ghost"; text: "Refresh"; onClicked: app.refreshPresets() }
                        }
                        Repeater {
                            model: app.presets
                            delegate: Rectangle {
                                required property var modelData
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
                                    Mono { text: modelData.id; color: Theme.text2; Layout.preferredWidth: 28 }
                                    Text { text: modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Btn { text: "Go"; variant: "ghost"; compact: true; onClicked: { root.presetIndex = index; ptz.gotoPreset(modelData.id, modelData.name) } }
                                }
                                TapHandler { onTapped: root.presetIndex = index }
                            }
                        }
                        Text {
                            visible: app.presets.length === 0
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 12
                            text: "No presets are stored on the camera. Point it where you want, then press Add."
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Btn {
                                text: "Set current"; variant: "ghost"; Layout.fillWidth: true
                                enabled: root.curPreset() !== null
                                onClicked: { var p = root.curPreset(); if (p) ptz.setPreset(p.id, p.name) }
                            }
                            Btn {
                                text: "Rename"; variant: "ghost"; Layout.fillWidth: true
                                enabled: root.curPreset() !== null
                                onClicked: { var p = root.curPreset(); if (p) { root.renameAdd = false; renameField.text = p.name; renamePop.open() } }
                            }
                            Btn {
                                text: "Add"; variant: "ghost"; Layout.fillWidth: true
                                onClicked: { root.renameAdd = true; renameField.text = ""; renamePop.open() }
                            }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                            text: "Select a row, then Set current to store the camera's present position under that number. Add stores it under the next free number." }
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
                            Lbl {
                                color: Theme.text
                                text: root.renameAdd ? "Add preset " + app.nextPresetId() + " at the current view"
                                                     : "Rename preset " + (root.curPreset() ? root.curPreset().id : "")
                            }
                            TextField {
                                id: renameField
                                implicitWidth: 260
                                color: Theme.text
                                placeholderText: "Name"
                                placeholderTextColor: Theme.text2
                                background: Rectangle { radius: 8; color: Theme.s2; border.color: Theme.border }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                                text: root.renameAdd ? "Stores where the camera is pointing now."
                                      : (app.canRenamePreset && app.presetsFromCamera ? "The name is changed on the camera too." : "This camera protocol cannot rename: the name is kept on this laptop only.")
                            }
                            RowLayout {
                                Btn { text: "Cancel"; variant: "ghost"; onClicked: renamePop.close() }
                                Btn {
                                    text: "Save"; variant: "primary"
                                    onClicked: {
                                        var p = root.curPreset()
                                        if (root.renameAdd) app.addPreset(renameField.text)
                                        else if (p) app.renamePreset(p.id, renameField.text)
                                        renamePop.close()
                                    }
                                }
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
                            model: app.auxItems
                            delegate: ToggleRow {
                                required property var modelData
                                readonly property var st: ptz.auxStates[modelData.id]
                                Layout.fillWidth: true
                                label: modelData.id === "ir" && root.isCj3000 ? modelData.label + " · no illuminator fitted (per spec)"
                                                            : modelData.id === "wiper" ? modelData.label + " · optional hardware" : modelData.label
                                enabled: modelData.supported
                                opacity: modelData.supported ? 1 : 0.55
                                unknown: st === undefined
                                isOn: st === true
                                hint: !modelData.supported ? "Not mapped yet · add it to the camera profile"
                                      : st === undefined ? (modelData.readable ? "The camera has not reported its state yet" : "State cannot be read back from this camera") : ""
                                onToggledByUser: (v) => ptz.aux(modelData.id, v, modelData.label)
                            }
                        }
                        Btn { text: "Return gimbal to centre"; variant: "ghost"; Layout.fillWidth: true; onClicked: ptz.home() }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                            text: "A switch only changes when the camera accepts the command; if it refuses, it flips back and the last command bar says why. Switches without a mapped endpoint are greyed out. Map them in the camera profile (Settings › Endpoint tester)." }
                    }
                }

                // ---------- HEALTH ----------
                Flickable {
                    contentHeight: healthCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: healthCol
                        width: parent.width
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Dot { color: root.levelColor(health.overall) }
                            Lbl { Layout.fillWidth: true; color: Theme.text; text: "Camera · " + (health.overall === "unknown" ? "no reading" : health.overall) }
                            Btn { compact: true; variant: "ghost"; text: health.busy ? "Reading…" : "Refresh"; enabled: !health.busy; onClicked: health.refresh() }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            color: health.stale ? Theme.warn : Theme.text2; font.pixelSize: 11
                            text: !health.available ? "This camera protocol has no health items. Add them to the camera profile (Settings › Endpoint tester)."
                                  : health.stale ? "Stale: the link is " + link.state + ", so these are the last values (read " + health.updated + " Z)."
                                  : health.updated.length ? "Read at " + health.updated + " Z · every 15 s while the link is good"
                                  : "Waiting for the first reading…"
                        }
                        Repeater {
                            model: health.items
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 40; radius: 8; color: Theme.s2
                                border.width: 1
                                border.color: modelData.level === "crit" ? Theme.crit : modelData.level === "warn" ? Theme.warn : Theme.border
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                                    Dot { color: root.levelColor(modelData.level) }
                                    Text { text: modelData.label; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Mono { text: modelData.error.length ? modelData.error : modelData.text; color: modelData.level === "crit" ? Theme.crit : modelData.level === "warn" ? Theme.warn : Theme.text }
                                }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                        Lbl { text: "This laptop's view"; font.pixelSize: 11 }
                        Repeater {
                            model: root.appHealth
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
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                            text: "Changes to warning or critical, and recoveries, are written to the audit log. Gimbal temperature and faults appear here once their endpoints are added to the camera profile."
                        }
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
                    Btn { Layout.fillWidth: true; variant: "ghost"; text: "Open the log and export folders"; onClicked: { app.openFolder("exports"); app.openFolder("logs") } }
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
                    model: [["WASD", "pan/tilt"], ["Q E", "zoom"], ["Space", "stop"], ["1–9", "preset"], ["T", "thermal"], ["P", "point"], ["H", "hide controls"], ["F11", "full screen"], ["N", "night"]]
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

    // ================= FOOTER =================
    Rectangle {
        id: statusBar
        visible: root.ov
        x: 0; y: root.height - height
        width: root.width
        height: 30
        color: Qt.rgba(Theme.pageBg.r, Theme.pageBg.g, Theme.pageBg.b, 0.94)
        Rectangle { width: parent.width; height: 1; color: Theme.pageBorder }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
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
            Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Control: " + (ptz.controlHeld ? ptz.operatorName + " (operator)" : (root.teamControl && control.holderName.length ? control.holderName + " (other laptop)" : "released")) }
            Mono { onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Heading — (MAVLink not linked)" }
        }
    }

    // ================= TEAM CONTROL =================
    Connections {
        target: control
        function onNotice(text, kind) { root.notice = text; noticeTimer.restart() }
        function onRequestChanged() { if (control.requesterName.length > 0) reqPop.open(); else reqPop.close() }
    }
    Popup {
        id: reqPop
        parent: root
        anchors.centerIn: parent
        padding: 18
        closePolicy: Popup.NoAutoClose
        background: Card { border.color: Theme.warn }
        contentItem: ColumnLayout {
            spacing: 12
            Lbl { text: "Control request"; color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.preferredWidth: 320; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13
                text: control.requesterName + " on " + control.requesterHost + " is asking to take control of " + (app.activeCamera.name || "this camera") + ". Handing over stops the camera first."
            }
            RowLayout {
                spacing: 8
                Btn { text: "Keep control"; variant: "ghost"; Layout.fillWidth: true; onClicked: control.deny() }
                Btn { text: "Hand over"; variant: "primary"; Layout.fillWidth: true; onClicked: control.grant() }
            }
        }
    }
    Popup {
        id: takeOverPop
        modal: true
        parent: root
        anchors.centerIn: parent
        padding: 18
        background: Card { border.color: Theme.crit }
        contentItem: ColumnLayout {
            spacing: 12
            Lbl { text: "Take over control"; color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.preferredWidth: 320; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13
                text: control.holderName + " is still in control" + (control.state === "requesting" && control.requestTimedOut ? " and has not answered" : "")
                      + ". Taking over locks their controls at once. Use it when they cannot respond."
            }
            RowLayout {
                spacing: 8
                Btn { text: "Cancel"; variant: "ghost"; Layout.fillWidth: true; onClicked: takeOverPop.close() }
                Btn { text: "Take over"; variant: "danger"; Layout.fillWidth: true; onClicked: { control.takeOver(); takeOverPop.close() } }
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
