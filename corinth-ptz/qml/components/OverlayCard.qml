import QtQuick
import QtQuick.Effects
import Corinth

// A panel that floats over the live video. It always uses the dark charcoal card tokens,
// whatever the page theme (it sits on a picture, not on a page); Night keeps its red palette.
// Slightly see-through so the picture is still there behind it, and optionally blurred.
// It takes the mouse: clicks on it never reach the picture underneath.
Rectangle {
    id: oc
    property real fill: 0.85          // how opaque the card is
    property Item blurSource: null    // the picture layer behind it
    property bool blur: false         // background blur (Settings › App › Appearance)

    radius: Theme.radiusCard
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, fill)
    border.width: 1
    border.color: Theme.border

    // Blurred copy of the picture behind this card, cut to its rounded shape.
    ShaderEffectSource {
        id: behind
        visible: false
        sourceItem: oc.blur ? oc.blurSource : null
        sourceRect: Qt.rect(oc.x, oc.y, oc.width, oc.height)
    }
    Item {
        id: shape
        anchors.fill: parent
        visible: false
        layer.enabled: oc.blur
        Rectangle { anchors.fill: parent; radius: oc.radius }
    }
    MultiEffect {
        anchors.fill: parent
        visible: oc.blur
        source: behind
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1.0
        blurMax: 40
        maskEnabled: true
        maskSource: shape
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (w) => w.accepted = true
    }
}
