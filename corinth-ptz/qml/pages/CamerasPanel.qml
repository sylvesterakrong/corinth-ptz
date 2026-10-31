import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Settings > App > Cameras: saved cameras, connection editor, network discovery.
ColumnLayout {
    id: panel
    spacing: 16
    signal displayFound(var item)

    property bool editing: false
    property var form: ({})
    property bool isExisting: false
    property string testState: ""      // "", "testing", "ok", "fail"
    property string testText: ""
    property string removeConfirm: ""

    function startEdit(f, existing) {
        form = f
        isExisting = existing
        testState = ""; testText = ""
        editing = true
    }
    function editCamera(c) {
        startEdit({ id: c.id, name: c.name, proto: c.proto, ip: c.ip, httpPort: String(c.httpPort), rtspPort: String(c.rtspPort),
                    mainUrl: c.mainUrl, subUrl: c.subUrl, user: c.user, pass: "", shown: c.shown, hasPassword: c.hasPassword }, true)
    }
    function editFound(item) {
        var isapi = item.protos.indexOf("ISAPI") >= 0
        startEdit({ id: "found-" + item.ip, name: item.model + " " + item.ip.split(".").pop(), proto: isapi ? "ISAPI" : "ONVIF",
                    ip: item.ip, httpPort: "80", rtspPort: "554", mainUrl: "", subUrl: "", user: "admin", pass: "", shown: true, hasPassword: false }, false)
    }
    function setF(k, v) { var n = Object.assign({}, form); n[k] = v; form = n; testState = "" }
    function payload(shown) {
        var p = Object.assign({}, form)
        p.httpPort = parseInt(form.httpPort) || 80
        p.rtspPort = parseInt(form.rtspPort) || 554
        if (shown !== undefined) p.shown = shown
        return p
    }
    function save(display) {
        var id = app.saveCamera(payload(display ? true : form.shown))
        if (display) app.setActive(id)
        editing = false
    }

    // ---------------- saved cameras ----------------
    Card {
        Layout.fillWidth: true
        implicitHeight: savedCol.implicitHeight + 40
        ColumnLayout {
            id: savedCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Saved cameras"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Btn {
                    variant: "ghost"; compact: true; text: "Add by URL"
                    onClicked: panel.startEdit({ id: "", name: "", proto: "ISAPI", ip: "", httpPort: "80", rtspPort: "554", mainUrl: "", subUrl: "", user: "admin", pass: "", shown: true, hasPassword: false }, false)
                }
            }
            Repeater {
                model: app.cameras
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 58
                    radius: 8
                    color: Theme.s2
                    border.width: 1
                    border.color: modelData.id === app.activeId ? Theme.accent : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10
                        Dot { color: Theme.good }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.name + (modelData.id === app.activeId ? "  ·  active" : ""); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                            Mono { text: (modelData.proto === "Demo" ? "Simulated" : modelData.ip) + " · " + modelData.proto + (modelData.proto !== "Demo" && !modelData.hasPassword ? " · no password set" : ""); color: Theme.text2; font.pixelSize: 11 }
                        }
                        Btn {
                            compact: true
                            variant: "ghost"
                            text: ""
                            implicitWidth: shownRow.implicitWidth + 20
                            onClicked: app.setCameraShown(modelData.id, !modelData.shown)
                            Accessible.name: "Show in live view"
                            contentItem: RowLayout {
                                id: shownRow
                                spacing: 8
                                SwitchPill { checked: modelData.shown; implicitWidth: 30; implicitHeight: 16 }
                                Text { text: modelData.shown ? "In live view" : "Hidden"; color: Theme.text; font.pixelSize: 12 }
                            }
                        }
                        Btn { compact: true; variant: "ghost"; text: "Use"; visible: modelData.id !== app.activeId; onClicked: app.setActive(modelData.id) }
                        Btn { compact: true; variant: "ghost"; text: "Edit"; onClicked: panel.editCamera(modelData) }
                        Btn {
                            compact: true
                            variant: "danger"
                            visible: app.cameras.length > 1
                            text: panel.removeConfirm === modelData.id ? "Confirm" : "Remove"
                            onClicked: {
                                if (panel.removeConfirm !== modelData.id) { panel.removeConfirm = modelData.id; return }
                                panel.removeConfirm = ""
                                app.removeCamera(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------- editor ----------------
    Card {
        visible: panel.editing
        Layout.fillWidth: true
        border.color: Theme.accent
        implicitHeight: editCol.implicitHeight + 40
        ColumnLayout {
            id: editCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: panel.isExisting ? "Edit camera connection" : "Add camera"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Seg {
                    small: true
                    model: [{ id: "ISAPI", label: "ISAPI" }, { id: "ONVIF", label: "ONVIF" }, { id: "RTSP only", label: "RTSP only" }, { id: "Demo", label: "Demo" }]
                    current: panel.form.proto || "ISAPI"
                    onPicked: (id) => panel.setF("proto", id)
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width > 620 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                TextRow { Layout.fillWidth: true; label: "Display name"; mono: false; value: panel.form.name || ""; onEdited: (v) => panel.setF("name", v) }
                TextRow { Layout.fillWidth: true; label: "IP address"; value: panel.form.ip || ""; placeholder: "192.168.1.64"; onEdited: (v) => panel.setF("ip", v) }
                TextRow { Layout.fillWidth: true; label: "HTTP port"; value: panel.form.httpPort || "80"; onEdited: (v) => panel.setF("httpPort", v) }
                TextRow { Layout.fillWidth: true; label: "RTSP port"; value: panel.form.rtspPort || "554"; onEdited: (v) => panel.setF("rtspPort", v) }
                TextRow {
                    Layout.fillWidth: true; label: "Main stream URL (optional override)"; value: panel.form.mainUrl || ""
                    placeholder: panel.form.proto === "ONVIF" ? "Read from the camera (GetStreamUri)" : panel.form.proto === "ISAPI" ? "rtsp://IP:554/Streaming/Channels/101" : "rtsp://…"
                    onEdited: (v) => panel.setF("mainUrl", v)
                }
                TextRow {
                    Layout.fillWidth: true; label: "Sub stream URL (optional override)"; value: panel.form.subUrl || ""
                    placeholder: panel.form.proto === "ONVIF" ? "Read from the camera (GetStreamUri)" : panel.form.proto === "ISAPI" ? "rtsp://IP:554/Streaming/Channels/102" : "rtsp://…"
                    onEdited: (v) => panel.setF("subUrl", v)
                }
                TextRow { Layout.fillWidth: true; label: "Username"; value: panel.form.user || ""; onEdited: (v) => panel.setF("user", v) }
                TextRow {
                    Layout.fillWidth: true; label: "Password"; password: true; value: panel.form.pass || ""
                    placeholder: panel.isExisting && panel.form.hasPassword ? "Leave blank to keep the saved password" : ""
                    onEdited: (v) => panel.setF("pass", v)
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text2
                font.pixelSize: 11
                text: "Credentials are added to the stream address automatically. Wrong passwords fail fast, but repeated failures can lock the camera account."
            }
            Rectangle {
                visible: panel.testState.length > 0
                Layout.fillWidth: true
                implicitHeight: 36
                radius: 8
                color: Theme.s2
                border.width: 1
                border.color: panel.testState === "ok" ? Theme.good : panel.testState === "fail" ? Theme.crit : Theme.accent
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                    Dot { color: panel.testState === "ok" ? Theme.good : panel.testState === "fail" ? Theme.crit : Theme.accent }
                    Text { text: panel.testState === "testing" ? "Testing connection…" : panel.testText; color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Btn {
                    variant: "ghost"
                    text: "Test connection"
                    enabled: panel.testState !== "testing"
                    onClicked: {
                        panel.testState = "testing"
                        app.testConnection(panel.payload(), function (ok, msg) { panel.testState = ok ? "ok" : "fail"; panel.testText = msg })
                    }
                }
                Item { Layout.fillWidth: true }
                Btn { variant: "ghost"; text: "Cancel"; onClicked: panel.editing = false }
                Btn { variant: "ghost"; text: "Save"; enabled: (panel.form.ip || "").length > 0 || panel.form.proto === "Demo"; onClicked: panel.save(false) }
                Btn { variant: "primary"; text: "Save and display"; enabled: (panel.form.ip || "").length > 0 || panel.form.proto === "Demo"; onClicked: panel.save(true) }
            }
        }
    }

    // ---------------- discovery ----------------
    Card {
        Layout.fillWidth: true
        implicitHeight: discCol.implicitHeight + 40
        ColumnLayout {
            id: discCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Lbl { text: "Cameras found on the network"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                Btn { variant: "ghost"; compact: true; text: discovery.scanning ? "Scanning…" : "Scan network"; enabled: !discovery.scanning; onClicked: discovery.scan() }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text2
                font.pixelSize: 12
                lineHeight: 1.3
                text: "ONVIF WS-Discovery from this computer. It finds cameras on the same network segment as the laptop. Discovery broadcasts do not cross Tailscale, so when you are remote, run the scan on the Jetson (see README) or add the camera by URL."
            }
            Rectangle {
                visible: discovery.results.length === 0
                Layout.fillWidth: true
                implicitHeight: 52
                radius: 8
                color: "transparent"
                border.width: 1
                border.color: Theme.border
                Text {
                    anchors.centerIn: parent
                    color: Theme.text2
                    font.pixelSize: 12
                    text: discovery.scanning ? "Scanning the network…" : (discovery.hasScanned ? "No ONVIF cameras answered. Check that ONVIF is enabled on the camera." : "No scan yet")
                }
            }
            Repeater {
                model: discovery.results
                delegate: Rectangle {
                    id: foundRow
                    required property var modelData
                    readonly property bool saved: app.cameras.some(function (c) { return c.ip === modelData.ip })
                    Layout.fillWidth: true
                    implicitHeight: 58
                    radius: 8
                    color: Theme.s2
                    border.width: 1
                    border.color: saved ? Theme.border : Theme.accent
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: 8
                                Text { text: modelData.model; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                                Rectangle {
                                    radius: 4
                                    color: foundRow.saved ? Theme.surface : Theme.accentSoft
                                    implicitWidth: tagText.implicitWidth + 12; implicitHeight: 18
                                    Lbl { id: tagText; anchors.centerIn: parent; text: foundRow.saved ? "Saved" : "New"; font.pixelSize: 10; color: Theme.text }
                                }
                            }
                            Mono { text: modelData.ip + " · " + modelData.vendor + " · " + modelData.protos; color: Theme.text2; font.pixelSize: 11 }
                        }
                        Btn { visible: !foundRow.saved; compact: true; variant: "ghost"; text: "Edit URL"; onClicked: panel.editFound(modelData) }
                        Btn { visible: !foundRow.saved; compact: true; variant: "primary"; text: "Display"; onClicked: panel.displayFound(modelData) }
                    }
                }
            }
        }
    }
}
