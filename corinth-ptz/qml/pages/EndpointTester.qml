import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth
import "../js/Schema.js" as Schema

// Settings > App > Endpoint tester: send any request to the active camera and see
// the raw answer, probe which standard endpoints exist, and manage the camera
// profile files that turn a captured endpoint into a working control.
ColumnLayout {
    id: tester
    spacing: 16

    property string method: "GET"
    property string path: "/ISAPI/System/deviceInfo"
    property string body: ""
    property bool busy: false
    property var result: null          // { ok, status, ms, text, size }
    property string confirmFor: ""     // "METHOD path" awaiting a second click
    property var history: []
    property var probes: []            // [{ path, status, ms }]
    property bool probing: false
    property string profileNote: ""

    readonly property bool dangerous: /reboot|factoryReset|format|upgrade|firmware|users|password|restore/i.test(path)
    readonly property bool writes: method !== "GET"
    readonly property bool blocked: writes && ptz.locked

    // paths worth asking about; all read-only
    readonly property var probeList: [
        "/ISAPI/System/deviceInfo", "/ISAPI/System/capabilities", "/ISAPI/System/status", "/ISAPI/System/time",
        "/ISAPI/System/Network/interfaces", "/ISAPI/System/Network/Integrate",
        "/ISAPI/Streaming/channels", "/ISAPI/Streaming/channels/101", "/ISAPI/Streaming/channels/201",
        "/ISAPI/Image/channels/1/color", "/ISAPI/Image/channels/1/capabilities", "/ISAPI/Image/channels/2/color",
        "/ISAPI/PTZCtrl/channels/1/capabilities", "/ISAPI/PTZCtrl/channels/1/status", "/ISAPI/PTZCtrl/channels/1/presets",
        "/ISAPI/PTZCtrl/channels/1/auxcontrols", "/ISAPI/PTZCtrl/channels/1/homeposition", "/ISAPI/PTZCtrl/channels/1/position3D/capabilities",
        "/ISAPI/Thermal/capabilities", "/ISAPI/Thermal/channels/2",
        "/ISAPI/ContentMgmt/Storage", "/ISAPI/Event/triggers", "/ISAPI/System/Hardware"
    ]

    Component.onCompleted: {
        try { history = JSON.parse(String(app.localValue("testerHistory", "[]"))) } catch (e) { history = [] }
        refreshInfo()
    }
    property var info: ({})
    function refreshInfo() { info = app.profileInfo() }
    Connections { target: app; function onActiveChanged() { tester.result = null; tester.probes = []; tester.refreshInfo() } }

    function key() { return method + " " + path }

    function send() {
        if (busy || path.length === 0) return
        if (blocked) { result = { ok: false, status: 0, ms: 0, text: "Not sent: this laptop is view-only or the link is lost. Take control first.", size: 0 }; return }
        if (writes && dangerous && confirmFor !== key()) { confirmFor = key(); return }
        confirmFor = ""
        busy = true
        var m = method, p = Schema.resolve(path, { ch: (app.profile.channel || 1), vch: app.channel })
        app.request(m, p, body, function (ok, status, text, ms) {
            tester.busy = false
            var shown = text.length > 200000 ? text.substr(0, 200000) + "\n… (truncated)" : text
            var pretty = shown.charAt(0) === "<" ? app.prettyXml(shown) : shown
            tester.result = { ok: ok, status: status, ms: ms, text: pretty, size: text.length }
            // the audit log keeps what was sent where, never the body (it can hold passwords)
            auditLog.add(ptz.operatorName, "Endpoint tester · " + m + " " + p, ok ? "HTTP " + status + " · " + ms + " ms" : "Failed · " + (status ? "HTTP " + status : "no answer"), ok ? "ok" : "warn")
            var h = tester.history.filter(function (e) { return e.method !== m || e.path !== p || e.body !== tester.body })
            h.unshift({ method: m, path: p, body: tester.body, status: status })
            tester.history = h.slice(0, 15)
            app.setLocalValue("testerHistory", JSON.stringify(tester.history))
        })
    }

    function probeAll() {
        if (probing) return
        probing = true
        probes = []
        var list = probeList.slice()
        // plus every GET path the camera profile knows
        var prof = app.profile
        function addProfilePaths(obj, depth) {
            if (!obj || typeof obj !== "object" || depth > 4) return
            if (typeof obj.path === "string" && String(obj.method || "GET").toUpperCase() === "GET")
                list.push(Schema.resolve(obj.path, { ch: (prof.channel || 1), vch: app.channel, id: 1 }))
            if (typeof obj.ep === "string") list.push(Schema.resolve(obj.ep, { ch: (prof.channel || 1), vch: app.channel }))
            for (var k in obj) addProfilePaths(obj[k], depth + 1)
        }
        addProfilePaths(prof.ptz, 0); addProfilePaths(prof.aux, 0); addProfilePaths(prof.health, 0)
        var seen = {}, queue = list.filter(function (p) { if (seen[p]) return false; seen[p] = true; return true })
        var i = 0
        function next() {
            if (i >= queue.length) { tester.probing = false; return }
            var p = queue[i++]
            app.request("GET", p, "", function (ok, status, text, ms) {
                var r = tester.probes.slice()
                r.push({ path: p, status: status, ms: ms, ok: ok })
                tester.probes = r
                probeTimer.restart()
            })
        }
        probeNext = next
        next()
    }
    property var probeNext: null
    Timer { id: probeTimer; interval: 150; onTriggered: if (tester.probeNext) tester.probeNext() }

    function load(e) { method = e.method; path = e.path; body = e.body || ""; confirmFor = "" }

    function snippet() {
        var o = { method: method, path: path }
        if (body.length) o.body = body
        return JSON.stringify(o, null, 2)
    }

    // ---------------- request ----------------
    Card {
        Layout.fillWidth: true
        implicitHeight: reqCol.implicitHeight + 40
        ColumnLayout {
            id: reqCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Request to " + (app.activeCamera.name || "the camera") + " (" + app.driverKind + ")"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Seg {
                    small: true
                    model: [{ id: "GET", label: "GET" }, { id: "PUT", label: "PUT" }, { id: "POST", label: "POST" }, { id: "DELETE", label: "DELETE" }]
                    current: tester.method
                    onPicked: (id) => { tester.method = id; tester.confirmFor = "" }
                }
            }
            TextRow {
                Layout.fillWidth: true
                label: "Path"
                value: tester.path
                placeholder: "/ISAPI/PTZCtrl/channels/1/status"
                onEdited: (v) => { tester.path = v; tester.confirmFor = "" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: tester.method !== "GET" && tester.method !== "DELETE"
                spacing: 6
                Lbl { text: "Body (XML)" }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 110
                    radius: Theme.radiusControl
                    color: Theme.s2
                    border.width: bodyEdit.activeFocus ? 2 : 1
                    border.color: bodyEdit.activeFocus ? Theme.accent : Theme.border
                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 2
                        TextArea {
                            id: bodyEdit
                            text: tester.body
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            wrapMode: TextEdit.WrapAnywhere
                            selectByMouse: true
                            background: null
                            onTextChanged: if (activeFocus) tester.body = text
                        }
                    }
                }
            }
            Text {
                visible: tester.blocked
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.warn; font.pixelSize: 12
                text: "View only: changes (PUT, POST, DELETE) are blocked until this laptop is in control and the link is up. GET still works."
            }
            Text {
                visible: tester.writes && !tester.blocked
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.text2; font.pixelSize: 12
                text: "This changes the camera. Send only what you captured from its own web page."
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Btn {
                    variant: tester.confirmFor === tester.key() ? "danger" : "primary"
                    enabled: !tester.busy && tester.path.length > 0
                    text: tester.busy ? "Sending…" : (tester.confirmFor === tester.key() ? "Click again to confirm" : "Send")
                    onClicked: tester.send()
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.text2; font.pixelSize: 11; wrapMode: Text.WordWrap
                    text: tester.confirmFor === tester.key() ? "This path can reboot, reset or erase the camera." : "{ch} and {vch} in the path are filled in for the viewed channel."
                }
            }
        }
    }

    // ---------------- response ----------------
    Card {
        visible: tester.result !== null
        Layout.fillWidth: true
        implicitHeight: resCol.implicitHeight + 40
        border.color: tester.result && tester.result.ok ? Theme.good : Theme.crit
        ColumnLayout {
            id: resCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Dot { color: tester.result && tester.result.ok ? Theme.good : Theme.crit }
                Lbl {
                    Layout.fillWidth: true
                    color: Theme.text; font.pixelSize: 13
                    text: tester.result ? (tester.result.status ? "HTTP " + tester.result.status : "No HTTP answer") + " · " + tester.result.ms + " ms · " + tester.result.size + " bytes" : ""
                }
                Btn { compact: true; variant: "ghost"; text: "Copy response"; onClicked: app.copyText(tester.result.text) }
                Btn { compact: true; variant: "ghost"; text: "Copy request as profile entry"; onClicked: { app.copyText(tester.snippet()); tester.profileNote = "Copied. Paste it into the profile file as the value of an entry (see below)." } }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.min(360, respText.implicitHeight + 20)
                radius: Theme.radiusControl
                color: Theme.s2
                border.width: 1
                border.color: Theme.border
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentWidth: width
                    contentHeight: respText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    TextEdit {
                        id: respText
                        width: parent.width
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        text: tester.result ? tester.result.text : ""
                    }
                }
            }
        }
    }

    // ---------------- history ----------------
    Card {
        visible: tester.history.length > 0
        Layout.fillWidth: true
        implicitHeight: histCol.implicitHeight + 40
        ColumnLayout {
            id: histCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Recent requests"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Btn { compact: true; variant: "ghost"; text: "Clear"; onClicked: { tester.history = []; app.setLocalValue("testerHistory", "[]") } }
            }
            Repeater {
                model: tester.history
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34; radius: 8; color: Theme.s2
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 10
                        Mono { text: modelData.method; color: modelData.method === "GET" ? Theme.text2 : Theme.warn; Layout.preferredWidth: 54 }
                        Mono { text: modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Mono { text: modelData.status ? String(modelData.status) : "—"; color: modelData.status >= 200 && modelData.status < 300 ? Theme.good : Theme.crit }
                    }
                    TapHandler { onTapped: tester.load(modelData) }
                }
            }
        }
    }

    // ---------------- probe ----------------
    Card {
        Layout.fillWidth: true
        implicitHeight: probeCol.implicitHeight + 40
        ColumnLayout {
            id: probeCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Which endpoints does this camera have?"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Btn { variant: "ghost"; compact: true; enabled: !tester.probing; text: tester.probing ? "Checking…" : "Check known endpoints"; onClicked: tester.probeAll() }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 12; lineHeight: 1.3
                text: "Asks for each standard path (read-only, one at a time) plus every path in the camera profile. 200 means it exists; 401 means the login is wrong; 404 means this firmware does not have it, so capture the real path from the camera's web page. Click a row to open it above."
            }
            Repeater {
                model: tester.probes
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 30; radius: 6; color: Theme.s2
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                        Dot { size: 6; color: modelData.ok ? Theme.good : (modelData.status === 401 ? Theme.warn : modelData.status === 404 ? Theme.text2 : Theme.crit) }
                        Mono { text: modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Mono { text: modelData.status ? String(modelData.status) : "no answer"; color: modelData.ok ? Theme.good : Theme.text2 }
                        Mono { text: modelData.ms + " ms"; color: Theme.text2 }
                    }
                    TapHandler { onTapped: { tester.load({ method: "GET", path: modelData.path, body: "" }) } }
                }
            }
        }
    }

    // ---------------- profile ----------------
    Card {
        Layout.fillWidth: true
        implicitHeight: profCol.implicitHeight + 40
        ColumnLayout {
            id: profCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 10
            Lbl { text: "Camera profile"; color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 12; lineHeight: 1.3
                text: "Every control (move, presets, wiper, health items, Settings fields) reads its path and XML from a profile. To wire a captured endpoint, add it to a profile file, save, and press Reload: no rebuild. Per-camera file wins over the all-cameras file, which wins over the built-in profile."
            }
            Rectangle {
                visible: String(tester.info.error || "").length > 0
                Layout.fillWidth: true
                implicitHeight: errT.implicitHeight + 16; radius: 8; color: Theme.critSoft; border.width: 1; border.color: Theme.crit
                Text { id: errT; anchors.fill: parent; anchors.margins: 8; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12; text: "A profile file could not be read and was ignored: " + (tester.info.error || "") }
            }
            Repeater {
                model: [
                    { n: "Loaded now", v: (tester.info.sources || []).join("   ›   ") || "(none: this protocol has no built-in profile)" },
                    { n: "This camera", v: (tester.info.cameraPath || "") + (tester.info.cameraExists ? "" : "   (not created yet)") },
                    { n: "All cameras", v: (tester.info.defaultPath || "") + (tester.info.defaultExists ? "" : "   (not created yet)") }
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    Lbl { text: modelData.n; font.pixelSize: 11 }
                    Mono { text: modelData.v; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Btn {
                    variant: "ghost"; text: "Create file for this camera"
                    onClicked: { var r = app.createProfileFile(true); tester.profileNote = r.ok ? (r.existed ? "Already exists: " : "Created: ") + r.path : "Could not create: " + r.error; tester.refreshInfo(); app.openProfileFolder() }
                }
                Btn {
                    variant: "ghost"; text: "Create file for all cameras"
                    onClicked: { var r = app.createProfileFile(false); tester.profileNote = r.ok ? (r.existed ? "Already exists: " : "Created: ") + r.path : "Could not create: " + r.error; tester.refreshInfo(); app.openProfileFolder() }
                }
                Btn { variant: "ghost"; text: "Open folder"; onClicked: app.openProfileFolder() }
                Item { Layout.fillWidth: true }
                Btn { variant: "primary"; text: "Reload profile"; onClicked: { app.reloadProfile(); tester.refreshInfo(); tester.profileNote = "Profile reloaded." } }
            }
            Text { visible: tester.profileNote.length > 0; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.accent; font.pixelSize: 12; text: tester.profileNote }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11; lineHeight: 1.3
                text: "Example, a stabilisation switch captured from the camera's page:\n{ \"aux\": { \"stab\": { \"on\": \"enable\", \"off\": \"disable\",\n  \"set\": { \"method\": \"PUT\", \"path\": \"/ISAPI/…\", \"body\": \"<…>{state}</…>\" },\n  \"get\": { \"method\": \"GET\", \"path\": \"/ISAPI/…\", \"tag\": \"…\" } } } }"
            }
        }
    }
}
