import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth
import "../js/Schema.js" as Schema

// Settings: Hikvision-style tree, change tracking across pages, ISAPI
// read-modify-write, camera discovery.
Rectangle {
    id: root
    color: Theme.pageBg
    property bool active: false
    signal back()
    signal openInput()

    property string openSec: "app"
    property string pageId: "cameras"
    property var vals: ({})
    property var saved: ({})
    property var xmlCache: ({})
    property var capsCache: ({})       // ep -> what the camera says it accepts (ONVIF cameras)
    property var readState: ({})      // ep -> "loading" | "ok" | "error: ..."
    property string confirmId: ""
    property string notice: ""
    property var toast: null           // { kind: "found" | "added" | "none", ... }
    property bool autoScanned: false
    property string loadedAt: ""

    readonly property var fieldIndex: {
        var m = {}
        var all = Schema.allFields()
        for (var i = 0; i < all.length; ++i) m[all[i].field.id] = all[i]
        return m
    }
    function pageFor(id) {
        for (var s = 0; s < Schema.sections.length; ++s)
            for (var p = 0; p < Schema.sections[s].pages.length; ++p)
                if (Schema.sections[s].pages[p].id === id) return { section: Schema.sections[s], page: Schema.sections[s].pages[p] }
        return { section: Schema.sections[0], page: Schema.sections[0].pages[0] }
    }
    readonly property var currentPage: pageFor(pageId)
    readonly property var changedIds: {
        var out = []
        for (var k in vals) if (String(vals[k]) !== String(saved[k])) out.push(k)
        return out
    }
    readonly property bool needsRestart: changedIds.some(function (id) { return fieldIndex[id] && fieldIndex[id].field.restart })

    // Channel the Video and Image pages edit: the one the live view shows.
    readonly property var pathCtx: ({ vch: app.channel, ch: (app.profile && app.profile.channel) || 1 })
    function hasOwn(o, k) { return !!o && Object.prototype.hasOwnProperty.call(o, k) }
    // A field with the camera profile's overrides applied (profile.settings[id]) and
    // its endpoint path resolved for the viewed channel. null in the profile unmaps it.
    function eff(f) {
        var ov = hasOwn(app.profile.settings, f.id) ? app.profile.settings[f.id] : undefined
        var src = ov === null ? undefined : (ov !== undefined ? Object.assign({}, f.src || {}, ov) : f.src)
        var unsupported = false
        if (src) {
            src = Object.assign({}, src)
            src.ep = Schema.resolve(src.ep, pathCtx)
            if (!app.settingsSupported(src.ep)) { unsupported = true; src = undefined }
        }
        return Object.assign({}, f, { src: src, unsupported: unsupported })
    }
    // The action behind a button: profile.actions[id] replaces the built-in one.
    function actOf(f) {
        var ov = hasOwn(app.profile.actions, f.id) ? app.profile.actions[f.id] : undefined
        if (ov === null) return undefined
        var a = ov !== undefined ? ov : f.act
        if (a && typeof a === "object") { a = Object.assign({}, a); a.path = Schema.resolve(a.path, pathCtx) }
        return a
    }

    readonly property string unsupportedText: app.driverKind === "RTSP only" ? "Not supported: this camera only supplies video"
                                                                                : "Not supported over " + app.driverKind
    readonly property string protocolBanner: app.driverKind === "RTSP only"
        ? "This camera only supplies video, so its settings cannot be sent to it. Values you enter are kept on this laptop only."
        : "This camera uses " + app.driverKind + ". Fields marked \"Not supported over " + app.driverKind + "\" are kept on this laptop only and are never sent to the camera."
    readonly property bool nonIsapi: app.driverKind !== "ISAPI" && app.driverKind !== "Demo"
    // true for a field this camera's protocol cannot read or write (so it is only kept on this laptop)
    function isUnsupported(f) {
        var e = eff(f)
        if (e.unsupported) return true
        return nonIsapi && !e.src && !e.local && currentPage.section.id !== "app" && ["text", "select", "slider", "toggle"].indexOf(f.type) >= 0
    }
    readonly property bool pageHasUnsupported: {
        var pg = currentPage.page
        for (var c = 0; c < pg.cards.length; ++c)
            for (var f = 0; f < pg.cards[c].fields.length; ++f)
                if (isUnsupported(pg.cards[c].fields[f])) return true
        return false
    }
    readonly property var readErrors: Object.keys(readState).filter(function (k) { return String(readState[k]).indexOf("error") === 0 })

    function fmt(v) { return typeof v === "boolean" ? (v ? "on" : "off") : String(v) }
    function pageChanged(pid) { return changedIds.some(function (id) { return fieldIndex[id] && fieldIndex[id].page.id === pid }) }
    function sectionChanged(sec) { return sec.pages.some(function (p) { return root.pageChanged(p.id) }) }

    function initValues() {
        var v = {}
        var all = Schema.allFields()
        for (var i = 0; i < all.length; ++i) {
            var f = all[i].field
            if (f.val === undefined) continue
            if (f.local) {
                var lv = app.localValue(f.local, f.val)
                if (f.type === "toggle") lv = (lv === true || String(lv) === "true")
                if (f.local === "operatorName" && String(lv) === "") lv = ptz.operatorName
                v[f.id] = lv
            } else v[f.id] = f.val
        }
        vals = v
        saved = Object.assign({}, v)
        xmlCache = {}
        capsCache = {}
        readState = {}
    }

    function setVal(id, v) {
        var n = Object.assign({}, vals); n[id] = v; vals = n
        notice = ""
    }

    // Read every wired endpoint on the current page.
    function loadPage() {
        var eps = {}
        var pg = pageFor(pageId).page   // not currentPage: it may not be re-evaluated yet
        for (var c = 0; c < pg.cards.length; ++c)
            for (var f = 0; f < pg.cards[c].fields.length; ++f) {
                var fd = eff(pg.cards[c].fields[f])
                if (fd.src) eps[fd.src.ep] = true
            }
        for (var ep in eps) readEndpoint(ep)
        if (app.driverKind === "ONVIF")
            for (var ep2 in eps) readCaps(ep2)
    }
    // The camera's own lists and ranges (ONVIF GetOptions), as <tag opt="a,b"/> and <tag min= max=/>.
    function readCaps(ep) {
        app.request("GET", ep + "/capabilities", "", function (ok, status, body) {
            if (!ok) return
            var c = Object.assign({}, root.capsCache); c[ep] = body; root.capsCache = c
        })
    }
    // A select's menu, narrowed to the values the camera accepts (when it has told us).
    function optsFor(f) {
        var base = f.opts || []
        var e = eff(f)
        if (!e.src) return base
        var caps = capsCache[e.src.ep]
        if (!caps) return base
        var m = new RegExp("<" + (e.src.res ? "videoResolution" : e.src.tag) + "\\b[^>]*\\bopt=\"([^\"]*)\"").exec(caps)
        if (!m) return base
        var allowed = m[1].split(",").map(function (s) { return s.trim().toLowerCase() })
        var out = base.filter(function (label) {
            var v = String(label)
            if (!e.src.res) {
                if (e.src.map && e.src.map[label] !== undefined) v = e.src.map[label]
                else if (e.src.scale) v = String(Math.round(Number(label) * e.src.scale))
            }
            return allowed.indexOf(String(v).toLowerCase()) >= 0
        })
        return out.length ? out : base
    }
    // A slider's range, from the camera when it has told us.
    function rangeOf(f, which) {
        var dflt = which === "min" ? (f.min || 0) : (f.max || 100)
        var e = eff(f)
        if (!e.src || !e.src.tag) return dflt
        var caps = capsCache[e.src.ep]
        if (!caps) return dflt
        var m = new RegExp("<" + e.src.tag + "\\b[^>]*\\b" + which + "=\"(-?\\d+)\"").exec(caps)
        return m ? Number(m[1]) : dflt
    }
    function readEndpoint(ep) {
        var rs = Object.assign({}, readState); rs[ep] = "loading"; readState = rs
        app.request("GET", ep, "", function (ok, status, body) {
            var r = Object.assign({}, root.readState)
            if (!ok) { r[ep] = "error: " + (status ? "HTTP " + status : body); root.readState = r; return }
            r[ep] = "ok"; root.readState = r
            var cache = Object.assign({}, root.xmlCache); cache[ep] = body; root.xmlCache = cache
            var nv = Object.assign({}, root.vals), ns = Object.assign({}, root.saved)
            for (var id in root.fieldIndex) {
                var fd = root.eff(root.fieldIndex[id].field)
                if (!fd.src || fd.src.ep !== ep) continue
                var v = Schema.fromXml(fd, body)
                if (v === null) continue
                var userChanged = String(nv[id]) !== String(ns[id])
                ns[id] = v
                if (!userChanged) nv[id] = v
            }
            root.saved = ns
            root.vals = nv
        })
    }

    function apply() {
        var ids = changedIds.slice()
        if (!ids.length) return
        var byEp = {}, localCount = 0, unmapped = [], anyUnsupported = false
        var ns = Object.assign({}, saved)
        for (var i = 0; i < ids.length; ++i) {
            var ix = fieldIndex[ids[i]], f = eff(ix.field)
            if (f.src) {
                (byEp[f.src.ep] = byEp[f.src.ep] || []).push(f)
            } else if (f.local) {
                app.setLocalValue(f.local, vals[f.id])
                if (f.local === "operatorName") ptz.operatorName = vals[f.id]
                ns[f.id] = vals[f.id]; localCount++
            } else {
                unmapped.push(f.label)
                if (isUnsupported(f)) anyUnsupported = true
                ns[f.id] = vals[f.id]
            }
        }
        saved = ns
        for (var ep in byEp) writeEndpoint(ep, byEp[ep])
        var parts = []
        if (Object.keys(byEp).length) parts.push("sending " + Object.keys(byEp).length + " update" + (Object.keys(byEp).length > 1 ? "s" : "") + " to the camera")
        if (localCount) parts.push(localCount + " saved on this laptop")
        if (unmapped.length) parts.push(unmapped.length + " not mapped to the camera yet (kept locally)")
        notice = parts.join(" · ")
        if (unmapped.length)
            auditLog.add(ptz.operatorName, "Settings not sent · " + unmapped.join(", "), anyUnsupported ? unsupportedText : "Not mapped to an ISAPI endpoint yet", "warn")
    }
    function writeEndpoint(ep, fields) {
        var base = xmlCache[ep]
        if (!base) {
            auditLog.add(ptz.operatorName, "Settings · " + ep, "Not sent · page was never read from the camera", "crit")
            notice = "Could not apply: " + ep + " was not read from the camera"
            return
        }
        var xml = base, labels = []
        for (var i = 0; i < fields.length; ++i) {
            var nx = Schema.toXml(fields[i], xml, vals[fields[i].id])
            if (nx === null) { auditLog.add(ptz.operatorName, "Settings · " + fields[i].label, "Not sent · camera XML has no such field", "warn"); continue }
            xml = nx
            labels.push(fields[i].label + " " + fmt(vals[fields[i].id]))
        }
        if (!labels.length) return
        app.request("PUT", ep, xml, function (ok, status, body, ms) {
            auditLog.add(ptz.operatorName, "Settings · " + labels.join(", "), ok ? "Ack " + ms + " ms" : "Failed · HTTP " + status, ok ? "ok" : "crit")
            if (ok) {
                root.notice = "Applied · re-reading from the camera"
                root.readEndpoint(ep)      // confirm what the camera actually kept
            } else {
                root.notice = "Camera rejected the change (HTTP " + status + "). Values restored."
                root.readEndpoint(ep)
            }
        })
    }
    function discard() { vals = Object.assign({}, saved); notice = "" }

    function runAction(f) {
        if (f.type === "link") { root.openInput(); return }
        var act = actOf(f)
        if (!act) { notice = f.label.replace("…", "") + " · not mapped yet"; return }
        if (f.danger && confirmId !== f.id) { confirmId = f.id; return }
        confirmId = ""
        if (typeof act === "string" && act.indexOf("app.open:") === 0) {
            app.openFolder(act.substr(9))
            notice = "Opened " + app.folderPath(act.substr(9))
            return
        }
        if (act === "ptz.setHome") { ptz.setHome(); notice = "Home position set"; return }
        if (act === "ptz.home") { ptz.home(); notice = "Going home"; return }
        if (ptz.locked) { notice = f.label.replace("…", "") + " · not sent: view only or link lost"; return }
        app.request(act.method, act.path, act.body || "", function (ok, status, body, ms) {
            auditLog.add(ptz.operatorName, f.label, ok ? "Ack " + ms + " ms" : "Failed · HTTP " + status, ok ? "ok" : "crit")
            root.notice = f.label + (ok ? " · sent" : " · failed (HTTP " + status + ")")
        })
    }

    // discovery -> toast
    Connections {
        target: discovery
        function onFinished(count) {
            var known = app.cameras.map(function (c) { return c.ip })
            var fresh = discovery.results.filter(function (r) { return known.indexOf(r.ip) < 0 })
            if (fresh.length) root.toast = { kind: "found", item: fresh[0], title: "New camera found", detail: fresh[0].ip + " · " + fresh[0].model + " · " + fresh[0].protos }
            else root.toast = { kind: "none", title: "Scan complete", detail: count ? "No new cameras: every camera found is already saved" : "No ONVIF cameras answered on this network" }
            toastTimer.restart()
        }
    }
    Timer { id: toastTimer; interval: 12000; onTriggered: root.toast = null }
    Timer { id: autoScan; interval: 1500; onTriggered: discovery.scan() }

    onActiveChanged: {
        if (!active) return
        loadedAt = new Date().toISOString().substr(11, 8)
        loadPage()
        if (!autoScanned) { autoScanned = true; autoScan.start() }
    }
    onPageIdChanged: { confirmId = ""; loadPage() }
    Connections { target: app; function onActiveChanged() { root.initValues(); root.loadPage() } }
    Connections { target: app; function onChannelChanged() { root.initValues(); root.loadPage() } }
    Component.onCompleted: initValues()

    function displayFound(item) {
        var id = app.saveCamera({ id: "found-" + item.ip, name: item.model + " " + item.ip.split(".").pop(), ip: item.ip,
                                  proto: item.protos.indexOf("ISAPI") >= 0 ? "ISAPI" : "ONVIF", user: item.protos.indexOf("ISAPI") >= 0 ? "admin" : "", httpPort: item.port || 80, shown: true })
        toast = { kind: "added", title: "Camera added", detail: item.ip + " is in the live view list. Add its password with Edit before use." }
        toastTimer.restart()
        return id
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // header
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
                ColumnLayout {
                    spacing: 0
                    Lbl { text: "Settings"; onPage: true; color: Theme.pageText; font.pixelSize: 16; font.letterSpacing: 3.2 }
                    Text { text: "Editing " + (app.activeCamera.name || "—") + " · " + (app.activeCamera.ip || "") + " · " + app.driverKind + (root.loadedAt ? " · opened " + root.loadedAt + " Z" : ""); color: Theme.pageText2; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
                Btn { onPage: true; text: discovery.scanning ? "Scanning…" : "Scan network"; enabled: !discovery.scanning; onClicked: { root.pageId = "cameras"; root.openSec = "app"; discovery.scan() } }
                Seg {
                    onPage: true; small: true
                    model: [{ id: "light", label: "Day" }, { id: "dark", label: "Dark" }, { id: "night", label: "Night" }]
                    current: app.theme
                    onPicked: (id) => app.theme = id
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 88
            spacing: 20

            // nav
            Card {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 12
                    contentHeight: navCol.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: navCol
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: Schema.sections
                            delegate: ColumnLayout {
                                required property var modelData
                                readonly property bool open: root.openSec === modelData.id
                                Layout.fillWidth: true
                                spacing: 2
                                Btn {
                                    Layout.fillWidth: true
                                    variant: "ghost"
                                    text: ""
                                    implicitHeight: 36
                                    onClicked: root.openSec = parent.open ? "" : modelData.id
                                    Accessible.name: modelData.label
                                    background: Rectangle { radius: 8; color: parent.parent.open ? Theme.s2 : "transparent" }
                                    contentItem: RowLayout {
                                        spacing: 8
                                        Text { text: modelData.label; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true }
                                        Dot { visible: root.sectionChanged(modelData); size: 6; color: Theme.accent }
                                        Text { text: "›"; color: Theme.text2; rotation: parent.parent.parent.open ? 90 : 0 }
                                    }
                                }
                                Repeater {
                                    model: parent.open ? modelData.pages : []
                                    delegate: Btn {
                                        required property var modelData
                                        readonly property bool cur: root.pageId === modelData.id
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 12
                                        implicitHeight: 32
                                        text: ""
                                        onClicked: root.pageId = modelData.id
                                        Accessible.name: modelData.label
                                        background: Rectangle {
                                            color: parent.cur ? Theme.accentSoft : "transparent"
                                            radius: 6
                                            Rectangle { width: 2; height: parent.height; color: parent.parent.cur ? Theme.accent : "transparent" }
                                        }
                                        contentItem: RowLayout {
                                            Text { text: modelData.label; color: parent.parent.cur ? Theme.text : Theme.text2; font.pixelSize: 13; Layout.fillWidth: true; leftPadding: 8 }
                                            Dot { visible: root.pageChanged(modelData.id); size: 6; color: Theme.accent }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // content
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: content.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: content
                    width: parent.width
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            spacing: 4
                            Lbl { text: root.currentPage.section.label; onPage: true; color: Theme.pageAccent; font.pixelSize: 11 }
                            Lbl { text: root.currentPage.page.label; onPage: true; color: Theme.pageText; font.pixelSize: 22; font.letterSpacing: 1.8 }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            visible: root.currentPage.page.ep.length > 0
                            radius: 8; color: Theme.pageS2; border.width: 1; border.color: Theme.pageBorder
                            implicitWidth: epText.implicitWidth + 20; implicitHeight: 30
                            Mono { id: epText; anchors.centerIn: parent; onPage: true; color: Theme.pageText2; font.pixelSize: 11; text: "Writes " + Schema.resolve(root.currentPage.page.ep, root.pathCtx) }
                        }
                        Seg {
                            visible: app.hasChannel2 && ["video", "image"].indexOf(root.currentPage.section.id) >= 0
                            onPage: true; small: true
                            model: [{ id: "1", label: "Visible" }, { id: "2", label: "Thermal" }]
                            current: String(app.channel)
                            onPicked: (id) => app.channel = parseInt(id)
                        }
                        Btn { visible: root.currentPage.page.ep.length > 0; onPage: true; compact: true; text: "Refresh from camera"; onClicked: root.loadPage() }
                    }

                    // one banner instead of an error per page that this protocol cannot serve
                    Rectangle {
                        visible: root.pageHasUnsupported && root.pageId !== "cameras" && root.pageId !== "tester"
                        Layout.fillWidth: true
                        implicitHeight: protoText.implicitHeight + 16
                        radius: 8; color: Theme.accentSoft; border.width: 1; border.color: Theme.accent
                        Text { id: protoText; anchors.fill: parent; anchors.margins: 8; wrapMode: Text.WordWrap; color: Theme.pageText; font.pixelSize: 12; text: root.protocolBanner }
                    }

                    // read errors: one box for the page, not one per endpoint
                    Rectangle {
                        visible: root.readErrors.length > 0
                        Layout.fillWidth: true
                        implicitHeight: errText.implicitHeight + 16
                        radius: 8; color: Theme.critSoft; border.width: 1; border.color: Theme.crit
                        Text {
                            id: errText
                            anchors.fill: parent; anchors.margins: 8; wrapMode: Text.WordWrap; color: Theme.pageText; font.pixelSize: 12
                            text: root.readErrors.length === 0 ? ""
                                  : root.readErrors.length === 1
                                    ? "Could not read " + root.readErrors[0] + " · " + root.readState[root.readErrors[0]] + ". Its fields show defaults and will not be sent."
                                    : "Could not read " + root.readErrors.length + " settings from the camera · " + root.readState[root.readErrors[0]]
                                      + ". Their fields show defaults and will not be sent. Check the camera is reachable, then press Refresh from camera."
                        }
                    }

                    CamerasPanel {
                        id: camPanel
                        visible: root.pageId === "cameras"
                        Layout.fillWidth: true
                        onDisplayFound: (item) => root.displayFound(item)
                    }

                    EndpointTester {
                        objectName: "endpointTester"
                        visible: root.pageId === "tester"
                        Layout.fillWidth: true
                    }

                    GridLayout {
                        visible: root.pageId !== "cameras" && root.pageId !== "tester"
                        Layout.fillWidth: true
                        columns: width > 760 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 16
                        Repeater {
                            model: root.currentPage.page.cards
                            delegate: Card {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: cardCol.implicitHeight + 40
                                ColumnLayout {
                                    id: cardCol
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 20
                                    spacing: 14
                                    Lbl { text: modelData.title; color: Theme.text; font.pixelSize: 13 }
                                    Repeater {
                                        model: modelData.fields
                                        delegate: ColumnLayout {
                                            id: fieldBox
                                            required property var modelData
                                            readonly property var f: modelData
                                            readonly property var v: root.vals[f.id]
                                            readonly property bool changed: f.val !== undefined && String(root.vals[f.id]) !== String(root.saved[f.id])
                                            readonly property bool unmapped: !root.eff(f).src && !f.local && root.currentPage.section.id !== "app" && ["text", "select", "slider", "toggle"].indexOf(f.type) >= 0
                                            Layout.fillWidth: true
                                            spacing: 4

                                            ToggleRow {
                                                visible: fieldBox.f.type === "toggle"
                                                Layout.fillWidth: true
                                                label: fieldBox.f.label
                                                hint: fieldBox.f.hint || ""
                                                warnWhenOn: !!fieldBox.f.warnOn
                                                isOn: fieldBox.v === true
                                                changed: fieldBox.changed
                                                onToggledByUser: (val) => root.setVal(fieldBox.f.id, val)
                                            }
                                            SliderRow {
                                                visible: fieldBox.f.type === "slider"
                                                Layout.fillWidth: true
                                                label: fieldBox.f.label
                                                from: root.rangeOf(fieldBox.f, "min")
                                                to: root.rangeOf(fieldBox.f, "max")
                                                unit: fieldBox.f.unit || ""
                                                value: Number(fieldBox.v) || 0
                                                changed: fieldBox.changed
                                                onMoved: (val) => root.setVal(fieldBox.f.id, Math.round(val))
                                            }
                                            DropField {
                                                visible: fieldBox.f.type === "select"
                                                Layout.fillWidth: true
                                                label: fieldBox.f.label
                                                model: root.optsFor(fieldBox.f)
                                                value: String(fieldBox.v)
                                                changed: fieldBox.changed
                                                restart: !!fieldBox.f.restart
                                                onPicked: (val) => root.setVal(fieldBox.f.id, val)
                                            }
                                            TextRow {
                                                visible: fieldBox.f.type === "text"
                                                Layout.fillWidth: true
                                                label: fieldBox.f.label
                                                password: !!fieldBox.f.secret
                                                value: fieldBox.v === undefined ? "" : String(fieldBox.v)
                                                changed: fieldBox.changed
                                                restart: !!fieldBox.f.restart
                                                onEdited: (val) => root.setVal(fieldBox.f.id, val)
                                            }
                                            RowLayout {
                                                visible: fieldBox.f.type === "ro"
                                                Layout.fillWidth: true
                                                Lbl { text: fieldBox.f.label; Layout.fillWidth: true }
                                                Mono { text: fieldBox.v === undefined ? "—" : String(fieldBox.v) }
                                            }
                                            Rectangle { visible: fieldBox.f.type === "ro"; Layout.fillWidth: true; height: 1; color: Theme.border }
                                            Btn {
                                                visible: fieldBox.f.type === "action" || fieldBox.f.type === "link"
                                                Layout.fillWidth: true
                                                implicitHeight: 40
                                                variant: fieldBox.f.type === "link" ? "primary" : (fieldBox.f.danger ? "danger" : "ghost")
                                                text: root.confirmId === fieldBox.f.id ? "Click again to confirm: " + fieldBox.f.label : fieldBox.f.label
                                                onClicked: root.runAction(fieldBox.f)
                                                background: Rectangle {
                                                    radius: 8
                                                    color: root.confirmId === fieldBox.f.id ? Theme.critSoft : (fieldBox.f.type === "link" ? Theme.primary : "transparent")
                                                    border.width: fieldBox.f.type === "link" ? 0 : 1
                                                    border.color: fieldBox.f.danger || root.confirmId === fieldBox.f.id ? Theme.crit : Theme.border
                                                }
                                            }
                                            Text {
                                                visible: fieldBox.f.type === "note"
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                color: Theme.text2; font.pixelSize: 12; lineHeight: 1.3
                                                text: fieldBox.f.label || ""
                                            }
                                            Repeater {
                                                model: fieldBox.f.type === "list" ? fieldBox.f.rows : []
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    implicitHeight: 38; radius: 8; color: Theme.s2
                                                    RowLayout {
                                                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                                        Text { text: modelData.a; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true }
                                                        Text { text: modelData.b; color: Theme.text2; font.pixelSize: 12 }
                                                    }
                                                }
                                            }
                                            Text {
                                                visible: fieldBox.unmapped
                                                text: root.isUnsupported(fieldBox.f) ? root.unsupportedText : "Not mapped to the camera yet"
                                                color: Theme.text2
                                                font.pixelSize: 11
                                                font.italic: true
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Card {
                        visible: root.pageId === "teamctl"
                        Layout.fillWidth: true
                        implicitHeight: teamCol.implicitHeight + 40
                        ColumnLayout {
                            id: teamCol
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 20
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Lbl { text: "Status · " + (app.activeCamera.name || "no camera"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                                Lbl { text: control.enabled ? control.state : "off"; color: control.mine ? Theme.good : Theme.text2; font.pixelSize: 11 }
                            }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 12; text: control.status }
                            Repeater {
                                model: control.operators
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 38; radius: 8; color: Theme.s2
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                        Dot { color: modelData.role === "in control" ? Theme.good : Theme.text2 }
                                        Text { text: modelData.name + (modelData.me ? "  (this laptop)" : ""); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true }
                                        Text { text: modelData.host; color: Theme.text2; font.pixelSize: 12 }
                                        Text { text: modelData.role; color: modelData.role === "in control" ? Theme.good : Theme.text2; font.pixelSize: 12 }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text2; font.pixelSize: 11
                                text: "Messages are signed with the team key; anything unsigned or signed with another key is ignored. Hand-over is by request; Take over is the override for a holder who does not answer. Changes to the key, peers or port apply when you press Apply."
                            }
                        }
                    }
                }
            }
        }
    }

    // pending changes bar
    Card {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        implicitHeight: 60
        border.color: root.changedIds.length ? Theme.accent : Theme.cardBorder
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 12
            Dot { color: root.changedIds.length ? Theme.accent : Theme.good }
            Text {
                text: root.changedIds.length === 0 ? "In sync with camera" : root.changedIds.length + (root.changedIds.length === 1 ? " change pending" : " changes pending")
                color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.text2; font.pixelSize: 12
                text: root.notice.length ? root.notice
                      : root.changedIds.slice(0, 3).map(function (id) {
                            var ix = root.fieldIndex[id]
                            return ix.page.label + " › " + ix.field.label + ": " + root.fmt(root.saved[id]) + " → " + root.fmt(root.vals[id])
                        }).join(" · ") + (root.changedIds.length > 3 ? " · and " + (root.changedIds.length - 3) + " more" : "")
            }
            Lbl { visible: root.needsRestart; text: "Camera restart needed"; color: Theme.warn; font.pixelSize: 11 }
            Btn { text: "Discard"; variant: "ghost"; enabled: root.changedIds.length > 0; onClicked: root.discard() }
            Btn { text: "Apply"; variant: "primary"; enabled: root.changedIds.length > 0; onClicked: root.apply() }
        }
    }

    // discovery toast
    Toast {
        visible: root.toast !== null
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 76
        anchors.rightMargin: 24
        title: root.toast ? root.toast.title : ""
        detail: root.toast ? root.toast.detail : ""
        onDismissed: root.toast = null
        Btn {
            visible: root.toast !== null && root.toast.kind === "found"
            Layout.fillWidth: true
            variant: "primary"
            text: "Display"
            onClicked: root.displayFound(root.toast.item)
        }
        Btn {
            visible: root.toast !== null && root.toast.kind === "found"
            Layout.fillWidth: true
            variant: "ghost"
            text: "Edit URL"
            onClicked: { root.pageId = "cameras"; root.openSec = "app"; camPanel.editFound(root.toast.item); root.toast = null }
        }
        Btn {
            visible: root.toast !== null && root.toast.kind === "added"
            Layout.fillWidth: true
            variant: "ghost"
            text: "Open live view"
            onClicked: { root.toast = null; root.back() }
        }
    }
}
