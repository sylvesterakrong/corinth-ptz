.pragma library

// Settings pages, mirroring the Hikvision configuration tree.
//
// Field types: text, ro (read-only), select, slider, toggle, action, note, list, link
// Wiring:
//   src: { ep, tag, map?, scale?, res?, tagW?, tagH?, on?, off? }  -> read/written over ISAPI
//          (read-modify-write: GET the XML, change the tags, PUT it back)
//   local: "key"                                                  -> stored on this laptop
//   act:  { method, path } | "ptz.setHome" | "ptz.home"            -> action buttons
// Fields with neither src nor local are shown so the layout is complete, but
// are not yet mapped to the camera: capture the request from the camera's own
// web page (DevTools > Network) and add a `src` here.

var res = ["3840×2160", "2560×1440", "1920×1080", "1280×720"]
var subRes = ["1280×720", "704×576", "640×360"]
var fps = ["30", "25", "20", "15", "10"]

function stream(prefix, ch, defRes, resOpts, defCodec, defFps, defBr, brMax) {
    var ep = "/ISAPI/Streaming/channels/" + ch
    return [
        { id: prefix + "Res", type: "select", label: "Resolution", val: defRes, opts: resOpts, src: { ep: ep, res: true, tagW: "videoResolutionWidth", tagH: "videoResolutionHeight" } },
        { id: prefix + "Codec", type: "select", label: "Video encoding", val: defCodec, opts: ["H.265", "H.264"], src: { ep: ep, tag: "videoCodecType" } },
        { id: prefix + "Fps", type: "select", label: "Frame rate (fps)", val: defFps, opts: fps, src: { ep: ep, tag: "maxFrameRate", scale: 100 } },
        { id: prefix + "BrType", type: "select", label: "Bitrate type", val: "Variable", opts: ["Variable", "Constant"], src: { ep: ep, tag: "videoQualityControlType", map: { "Variable": "VBR", "Constant": "CBR" } } },
        { id: prefix + "Br", type: "slider", label: "Max bitrate", val: defBr, min: 128, max: brMax, unit: " kbps", src: { ep: ep, tag: "vbrUpperCap" } },
        { id: prefix + "Gop", type: "text", label: "I-frame interval", val: "50", src: { ep: ep, tag: "GovLength" } }
    ]
}

var sections = [
    { id: "system", label: "System", pages: [
        { id: "device", label: "Device info", ep: "/ISAPI/System/deviceInfo", cards: [
            { title: "Device", fields: [
                { id: "devName", type: "text", label: "Device name", val: "", src: { ep: "/ISAPI/System/deviceInfo", tag: "deviceName" } },
                { id: "model", type: "ro", label: "Model", val: "—", src: { ep: "/ISAPI/System/deviceInfo", tag: "model" } },
                { id: "serial", type: "ro", label: "Serial number", val: "—", src: { ep: "/ISAPI/System/deviceInfo", tag: "serialNumber" } },
                { id: "fw", type: "ro", label: "Firmware", val: "—", src: { ep: "/ISAPI/System/deviceInfo", tag: "firmwareVersion" } } ] } ] },
        { id: "time", label: "Time", ep: "/ISAPI/System/time", cards: [
            { title: "Clock", fields: [
                { id: "tsync", type: "select", label: "Sync mode", val: "NTP", opts: ["NTP", "Manual"], src: { ep: "/ISAPI/System/time", tag: "timeMode", map: { "NTP": "NTP", "Manual": "manual" } } },
                { id: "ntp", type: "text", label: "NTP server (Jetson LAN address)", val: "", src: { ep: "/ISAPI/System/time/ntpServers/1", tag: "ipAddress" } },
                { id: "ntpInt", type: "select", label: "Sync interval", val: "60 min", opts: ["15 min", "60 min", "1440 min"], src: { ep: "/ISAPI/System/time/ntpServers/1", tag: "synchronizeInterval", map: { "15 min": "15", "60 min": "60", "1440 min": "1440" } } },
                { id: "tz", type: "select", label: "Time zone", val: "UTC+00:00", opts: ["UTC+00:00", "UTC+01:00", "UTC-05:00"] },
                { id: "note1", type: "note", label: "Point the camera at the Jetson as NTP server so camera, Jetson and laptop share one clock for evidence timestamps." } ] } ] },
        { id: "maint", label: "Maintenance", ep: "/ISAPI/System/reboot · /factoryReset", cards: [
            { title: "Device actions", fields: [
                { id: "reboot", type: "action", label: "Reboot camera", danger: true, act: { method: "PUT", path: "/ISAPI/System/reboot" } },
                { id: "restore", type: "action", label: "Restore settings (keep network)", danger: true, act: { method: "PUT", path: "/ISAPI/System/factoryReset?mode=basic" } },
                { id: "factory", type: "action", label: "Factory default (erases everything)", danger: true, act: { method: "PUT", path: "/ISAPI/System/factoryReset?mode=full" } } ] },
            { title: "Firmware and config", fields: [
                { id: "fwUp", type: "action", label: "Upgrade firmware…" },
                { id: "cfgOut", type: "action", label: "Export configuration" },
                { id: "note2", type: "note", label: "Firmware upgrade and config export upload/download binary files; use the camera's own web page for these for now." } ] } ] },
        { id: "security", label: "Security", ep: "/ISAPI/Security", cards: [
            { title: "Authentication", fields: [
                { id: "rtspAuth", type: "select", label: "RTSP authentication", val: "digest", opts: ["digest", "digest/basic"] },
                { id: "webAuth", type: "select", label: "Web authentication", val: "digest", opts: ["digest", "digest/basic"] },
                { id: "lockout", type: "toggle", label: "Lock account after failed logins", val: true },
                { id: "ssh", type: "toggle", label: "SSH service", val: false, hint: "Keep off unless debugging", warnOn: true } ] } ] },
        { id: "users", label: "User management", ep: "/ISAPI/Security/users", cards: [
            { title: "Users", fields: [
                { id: "userList", type: "list", rows: [{ a: "admin", b: "Administrator" }, { a: "corinth", b: "Operator (app login)" }, { a: "onvif-corinth", b: "ONVIF user" }] },
                { id: "addUser", type: "action", label: "Add user…" },
                { id: "pwd", type: "action", label: "Change admin password…" } ] } ] }
    ] },
    { id: "network", label: "Network", pages: [
        { id: "tcpip", label: "TCP/IP", ep: "/ISAPI/System/Network/interfaces/1/ipAddress", cards: [
            { title: "IPv4", fields: [
                { id: "dhcp", type: "toggle", label: "DHCP", val: false, hint: "Use a fixed address on the boat LAN", src: { ep: "/ISAPI/System/Network/interfaces/1/ipAddress", tag: "addressingType", on: "dynamic", off: "static" }, restart: true },
                { id: "ip", type: "text", label: "IPv4 address", val: "", restart: true, src: { ep: "/ISAPI/System/Network/interfaces/1/ipAddress", tag: "ipAddress" } },
                { id: "mask", type: "text", label: "Subnet mask", val: "", restart: true, src: { ep: "/ISAPI/System/Network/interfaces/1/ipAddress", tag: "subnetMask" } },
                { id: "note3", type: "note", label: "Changing the address drops the connection. Update the camera entry under App › Cameras to the new address afterwards." } ] } ] },
        { id: "ports", label: "Port", ep: "/ISAPI/Security/adminAccesses", cards: [
            { title: "Service ports", fields: [
                { id: "pHttp", type: "text", label: "HTTP", val: "80", restart: true },
                { id: "pHttps", type: "text", label: "HTTPS", val: "443", restart: true },
                { id: "pRtsp", type: "text", label: "RTSP", val: "554", restart: true },
                { id: "pSdk", type: "text", label: "Server (SDK)", val: "8000", restart: true } ] } ] },
        { id: "https", label: "HTTPS", ep: "/ISAPI/Security/adminAccesses", cards: [
            { title: "HTTPS", fields: [
                { id: "httpsOn", type: "toggle", label: "Enable HTTPS", val: true },
                { id: "cert", type: "ro", label: "Certificate", val: "Self-signed" } ] } ] },
        { id: "integ", label: "Integration protocol", ep: "/ISAPI/System/Network/Integrate", cards: [
            { title: "Protocols", fields: [
                { id: "onvif", type: "toggle", label: "ONVIF", val: true, hint: "Needed for discovery and the ONVIF driver" },
                { id: "isapi", type: "toggle", label: "ISAPI", val: true },
                { id: "cloud", type: "toggle", label: "Vendor cloud / P2P access", val: false, hint: "Keep off: no internet egress from the camera", warnOn: true } ] } ] }
    ] },
    { id: "video", label: "Video", pages: [
        { id: "main", label: "Main stream", ep: "/ISAPI/Streaming/channels/101", cards: [
            { title: "Encoding", fields: stream("m", "101", "1920×1080", res, "H.265", "25", 4096, 8192) } ] },
        { id: "sub", label: "Sub stream", ep: "/ISAPI/Streaming/channels/102", cards: [
            { title: "Encoding", fields: stream("s", "102", "640×360", subRes, "H.264", "15", 768, 2048).concat([
                { id: "note4", type: "note", label: "The live view falls back to this stream automatically when the link degrades. Keep it under 1 Mbps for satellite." } ]) } ] }
    ] },
    { id: "image", label: "Image", pages: [
        { id: "display", label: "Display settings", ep: "/ISAPI/Image/channels/1", cards: [
            { title: "Image adjustment", fields: [
                { id: "bright", type: "slider", label: "Brightness", val: 50, min: 0, max: 100, src: { ep: "/ISAPI/Image/channels/1/color", tag: "brightnessLevel" } },
                { id: "contrast", type: "slider", label: "Contrast", val: 50, min: 0, max: 100, src: { ep: "/ISAPI/Image/channels/1/color", tag: "contrastLevel" } },
                { id: "sat", type: "slider", label: "Saturation", val: 50, min: 0, max: 100, src: { ep: "/ISAPI/Image/channels/1/color", tag: "saturationLevel" } },
                { id: "sharp", type: "slider", label: "Sharpness", val: 50, min: 0, max: 100, src: { ep: "/ISAPI/Image/channels/1/sharpness", tag: "SharpnessLevel" } } ] },
            { title: "Exposure and day/night", fields: [
                { id: "dayNight", type: "select", label: "Day/night switch", val: "Auto", opts: ["Auto", "Day", "Night", "Scheduled"], src: { ep: "/ISAPI/Image/channels/1/IrcutFilter", tag: "IrcutFilterType", map: { "Auto": "auto", "Day": "day", "Night": "night", "Scheduled": "schedule" } } },
                { id: "wb", type: "select", label: "White balance", val: "Auto", opts: ["Auto", "Outdoor", "Manual"], src: { ep: "/ISAPI/Image/channels/1/whiteBalance", tag: "WhiteBalanceStyle", map: { "Auto": "auto", "Outdoor": "outdoor", "Manual": "manual" } } },
                { id: "expMode", type: "select", label: "Exposure mode", val: "Auto", opts: ["Auto", "Iris priority", "Shutter priority", "Manual"] },
                { id: "shutter", type: "select", label: "Max shutter", val: "1/50", opts: ["1/25", "1/50", "1/100", "1/250"] } ] },
            { title: "Enhancement", fields: [
                { id: "wdr", type: "toggle", label: "WDR", val: false, hint: "Bright sky over dark water", src: { ep: "/ISAPI/Image/channels/1/WDR", tag: "mode", on: "open", off: "close" } },
                { id: "defog", type: "toggle", label: "Defog", val: true, hint: "Haze and sea spray" },
                { id: "eis", type: "toggle", label: "EIS", val: true, hint: "Electronic stabilisation" },
                { id: "dnr", type: "select", label: "Noise reduction", val: "Normal", opts: ["Off", "Normal", "Expert"] } ] } ] },
        { id: "osd", label: "OSD", ep: "/ISAPI/System/Video/inputs/channels/1/overlays", cards: [
            { title: "On-screen display", fields: [
                { id: "osdName", type: "toggle", label: "Show camera name", val: true },
                { id: "osdDate", type: "toggle", label: "Show date and time", val: true },
                { id: "osdPtz", type: "toggle", label: "Show PTZ position", val: false },
                { id: "osdText", type: "text", label: "Custom text", val: "ATLANTIS USV" } ] } ] },
        { id: "mask", label: "Privacy mask", ep: "/ISAPI/System/Video/inputs/channels/1/privacyMask", cards: [
            { title: "Privacy and no-look zones", fields: [
                { id: "maskOn", type: "toggle", label: "Enable privacy mask", val: false },
                { id: "note5", type: "note", label: "Masks follow the PTZ position, so a masked shoreline stays hidden as the camera moves. Drawing masks is done on the camera's web page for now." } ] } ] }
    ] },
    { id: "ptz", label: "PTZ", pages: [
        { id: "ptzBasic", label: "Basic", ep: "/ISAPI/PTZCtrl/channels/1", cards: [
            { title: "Movement", fields: [
                { id: "propPan", type: "toggle", label: "Proportional pan", val: true, hint: "Slower pan when zoomed in" },
                { id: "freeze", type: "toggle", label: "Preset freezing", val: false, hint: "Freeze image while moving to a preset" },
                { id: "autoFlip", type: "toggle", label: "Auto flip", val: false } ] } ] },
        { id: "limit", label: "Limit", ep: "/ISAPI/PTZCtrl/channels/1/limits", cards: [
            { title: "Pan and tilt limits", fields: [
                { id: "limOn", type: "toggle", label: "Enable limits", val: false, hint: "Keep the camera off the hull and mast" },
                { id: "note6", type: "note", label: "Set limits on the camera's web page for now; the app respects whatever the camera enforces." } ] } ] },
        { id: "initPos", label: "Initial position", ep: "/ISAPI/PTZCtrl/channels/1/homeposition", cards: [
            { title: "Home position", fields: [
                { id: "homeSet", type: "action", label: "Set current position as home", act: "ptz.setHome" },
                { id: "homeGo", type: "action", label: "Go to home", act: "ptz.home" },
                { id: "homeClr", type: "action", label: "Clear home position", danger: true, act: { method: "DELETE", path: "/ISAPI/PTZCtrl/channels/1/homeposition" } } ] } ] },
        { id: "park", label: "Park action", ep: "/ISAPI/PTZCtrl/channels/1/parkaction", cards: [
            { title: "When idle", fields: [
                { id: "parkOn", type: "toggle", label: "Enable park action", val: true },
                { id: "parkTime", type: "select", label: "Idle time before park", val: "60 s", opts: ["30 s", "60 s", "120 s", "300 s"] },
                { id: "parkAct", type: "select", label: "Action", val: "Preset 5", opts: ["Preset 1", "Preset 5", "Patrol 1", "Auto scan"] } ] } ] }
    ] },
    { id: "storage", label: "Storage", pages: [
        { id: "sd", label: "SD card", ep: "/ISAPI/ContentMgmt/Storage", cards: [
            { title: "Card", fields: [
                { id: "sdCap", type: "ro", label: "Capacity (MB)", val: "—", src: { ep: "/ISAPI/ContentMgmt/Storage", tag: "capacity" } },
                { id: "sdFree", type: "ro", label: "Free (MB)", val: "—", src: { ep: "/ISAPI/ContentMgmt/Storage", tag: "freeSpace" } },
                { id: "sdStatus", type: "ro", label: "Status", val: "—", src: { ep: "/ISAPI/ContentMgmt/Storage", tag: "status" } },
                { id: "sdFormat", type: "action", label: "Format SD card", danger: true, act: { method: "PUT", path: "/ISAPI/ContentMgmt/Storage/hdd/1/format" } } ] } ] }
    ] },
    { id: "app", label: "App (local)", pages: [
        { id: "cameras", label: "Cameras", ep: "", cards: [] },
        { id: "input", label: "Input", ep: "", cards: [
            { title: "Joystick and keyboard", fields: [
                { id: "note7", type: "note", label: "Detect controllers, pick the device type and assign buttons on the joystick page." },
                { id: "openInput", type: "link", label: "Open joystick setup" } ] } ] },
        { id: "recording", label: "Recording", ep: "", cards: [
            { title: "Files", fields: [
                { id: "recFolder", type: "text", label: "Recording folder (blank = Videos/Corinth)", val: "", local: "recFolder" },
                { id: "snapFolder", type: "text", label: "Snapshot folder (blank = Pictures/Corinth)", val: "", local: "snapFolder" },
                { id: "segLen", type: "select", label: "File segment length", val: "10 min", opts: ["5 min", "10 min", "30 min"], local: "segLen" },
                { id: "snapFmt", type: "select", label: "Snapshot format", val: "PNG", opts: ["PNG", "JPEG"], local: "snapFmt" } ] },
            { title: "Evidence", fields: [
                { id: "hashOn", type: "toggle", label: "Hash snapshots on capture (SHA-256)", val: true, local: "hashOn" },
                { id: "note8", type: "note", label: "Recording uses ffmpeg stream copy (no re-encode). KLV metadata and MAVLink geotags arrive with the MAVLink integration." } ] } ] },
        { id: "appearance", label: "Appearance", ep: "", cards: [
            { title: "Display", fields: [
                { id: "operatorName", type: "text", label: "Operator name (shown in the audit log)", val: "", local: "operatorName" },
                { id: "crossDef", type: "toggle", label: "Show crosshair by default", val: true, local: "crossDef" } ] } ] }
    ] }
]

function allFields() {
    var out = []
    for (var s = 0; s < sections.length; ++s)
        for (var p = 0; p < sections[s].pages.length; ++p)
            for (var c = 0; c < sections[s].pages[p].cards.length; ++c)
                for (var f = 0; f < sections[s].pages[p].cards[c].fields.length; ++f) {
                    var fd = sections[s].pages[p].cards[c].fields[f]
                    out.push({ field: fd, section: sections[s], page: sections[s].pages[p] })
                }
    return out
}

function tagValue(xml, tag) {
    var m = new RegExp("<(?:\\w+:)?" + tag + "(?:\\s[^>]*)?>([^<]*)</(?:\\w+:)?" + tag + ">").exec(xml)
    return m ? m[1].trim() : null
}

function setTag(xml, tag, value) {
    var re = new RegExp("(<(?:\\w+:)?" + tag + "(?:\\s[^>]*)?>)[^<]*(</(?:\\w+:)?" + tag + ">)")
    if (!re.test(xml)) return null
    return xml.replace(re, "$1" + String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;") + "$2")
}

// XML -> UI value (or null if the tag is missing)
function fromXml(f, xml) {
    var s = f.src
    if (s.res) {
        var w = tagValue(xml, s.tagW), h = tagValue(xml, s.tagH)
        return (w && h) ? (w + "×" + h) : null
    }
    var raw = tagValue(xml, s.tag)
    if (raw === null) return null
    if (f.type === "toggle") return raw.toLowerCase() === String(s.on).toLowerCase()
    if (s.map) {
        for (var k in s.map) if (String(s.map[k]).toLowerCase() === raw.toLowerCase()) return k
        return raw
    }
    if (s.scale) return String(Math.round(Number(raw) / s.scale))
    if (f.type === "slider") return Number(raw)
    return raw
}

// UI value -> XML (returns null if a tag is missing)
function toXml(f, xml, v) {
    var s = f.src
    if (s.res) {
        var parts = String(v).split("×")
        var x = setTag(xml, s.tagW, parts[0])
        return x === null ? null : setTag(x, s.tagH, parts[1])
    }
    var out
    if (f.type === "toggle") out = v ? s.on : s.off
    else if (s.map) out = s.map[v] !== undefined ? s.map[v] : v
    else if (s.scale) out = Math.round(Number(v) * s.scale)
    else out = v
    return setTag(xml, s.tag, out)
}
