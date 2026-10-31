pragma Singleton
import QtQuick

// Design tokens from the agreed scheme. Cards stay charcoal in every mode;
// only the page changes (near-black dark, Star Dust day, black night).
QtObject {
    id: theme
    property string mode: "dark"           // "dark" | "night" | "light"
    readonly property bool night: mode === "night"
    readonly property bool light: mode === "light"

    // ---- card tokens ------------------------------------------------------
    readonly property color surface:     night ? "#0B0706" : "#141414"
    readonly property color s2:          night ? "#160D0B" : "#1F1F1F"
    readonly property color border:      night ? "#2B1814" : "#2A2A2A"
    readonly property color cardBorder:  night ? "#2B1814" : (light ? Qt.rgba(1.0, 0.98, 0.973, 0.32) : "#2A2A2A")
    readonly property color text:        night ? "#E0917F" : "#F2EDEA"
    readonly property color text2:       night ? "#AD6E60" : "#A1A1A1"
    readonly property color accent:      night ? "#C7695A" : "#6C9BAD"
    readonly property color accentSoft:  night ? Qt.rgba(0.78, 0.41, 0.35, 0.16) : Qt.rgba(0.11, 0.243, 0.306, 0.55)
    readonly property color primary:     night ? "#5A2A22" : "#285C70"
    readonly property color onPrimary:   night ? "#E0917F" : "#F2EDEA"
    readonly property color good:        night ? "#B9A06A" : "#9CC5A1"
    readonly property color goodSoft:    night ? Qt.rgba(0.725, 0.627, 0.416, 0.14) : Qt.rgba(0.61, 0.77, 0.63, 0.12)
    readonly property color warn:        night ? "#D08A4A" : "#D4B86A"
    readonly property color warnSoft:    night ? Qt.rgba(0.816, 0.541, 0.29, 0.16) : Qt.rgba(0.83, 0.72, 0.416, 0.14)
    readonly property color crit:        night ? "#E0503F" : "#C8665E"
    readonly property color critSoft:    night ? Qt.rgba(0.878, 0.314, 0.247, 0.16) : Qt.rgba(0.784, 0.4, 0.37, 0.14)
    readonly property color knob:        night ? "#E0917F" : "#FFFAF8"

    // ---- page tokens ------------------------------------------------------
    readonly property color pageBg:      night ? "#000000" : (light ? "#F2EDEA" : "#090909")
    readonly property color pageS2:      night ? "#120A08" : (light ? "#FFFAF8" : "#1A1A1A")
    readonly property color pageBorder:  night ? "#22130F" : (light ? "#D8D3D1" : "#232323")
    readonly property color pageText:    night ? "#E0917F" : (light ? "#111111" : "#F2EDEA")
    readonly property color pageText2:   night ? "#AD6E60" : (light ? "#4A4A4A" : "#A1A1A1")
    readonly property color pageAccent:  night ? "#C7695A" : (light ? "#285C70" : "#6C9BAD")
    readonly property color pageGood:    night ? "#B9A06A" : (light ? "#3E7A52" : "#9CC5A1")
    readonly property color pageWarn:    night ? "#D08A4A" : (light ? "#8A6D1E" : "#D4B86A")
    readonly property color pageCrit:    night ? "#E0503F" : (light ? "#A84A43" : "#C8665E")
    // selected segment on the page
    readonly property color pageSegOn:   light ? "#141414" : pageBg
    readonly property color pageSegOnText: light ? "#F2EDEA" : pageText

    // ---- video overlays ---------------------------------------------------
    readonly property color overlayBg:       night ? Qt.rgba(0, 0, 0, 0.70) : Qt.rgba(0.035, 0.035, 0.035, 0.62)
    readonly property color overlayBgStrong: night ? Qt.rgba(0, 0, 0, 0.85) : Qt.rgba(0.035, 0.035, 0.035, 0.82)
    readonly property color overlayText:     night ? "#E0917F" : "#F2EDEA"
    readonly property color overlayText2:    night ? "#AD6E60" : "#A1A1A1"

    // ---- type -------------------------------------------------------------
    // Windows: Segoe UI, Bahnschrift (DIN-style, close to the SpaceX feel), Cascadia Mono.
    readonly property string uiFont: "Segoe UI"
    readonly property string labelFont: "Bahnschrift SemiCondensed"
    readonly property string monoFont: "Cascadia Mono"

    readonly property int radiusInner: 6
    readonly property int radiusControl: 8
    readonly property int radiusCard: 16
}
