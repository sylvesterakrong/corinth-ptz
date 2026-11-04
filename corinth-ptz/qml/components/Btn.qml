import QtQuick
import QtQuick.Controls.Basic
import Corinth

// Corinth button.
// variant: "soft" (filled control), "ghost" (outline), "primary" (the one filled
// action per area), "danger" (destructive), "toggle" (uses `on`)
Button {
    id: b
    property string variant: "soft"
    property bool on: false
    property bool onPage: false
    property color accentColor: onPage ? Theme.pageAccent : Theme.accent
    property int radiusPx: Theme.radiusControl
    property bool compact: false

    implicitHeight: compact ? 30 : 36
    leftPadding: compact ? 10 : 12
    rightPadding: compact ? 10 : 12
    hoverEnabled: true
    focusPolicy: Qt.TabFocus   // a click must not take the keyboard away from the live view (F11, H, WASD)
    font.family: Theme.uiFont
    font.pixelSize: compact ? 12 : 13
    Accessible.name: text

    readonly property color _fg: !enabled ? (onPage ? Theme.pageText2 : Theme.text2)
                                 : variant === "primary" ? Theme.onPrimary
                                 : variant === "danger" ? Theme.crit
                                 : (onPage ? Theme.pageText : Theme.text)
    readonly property color _bg: variant === "primary" ? Theme.primary
                                 : variant === "ghost" || variant === "danger" ? "transparent"
                                 : variant === "toggle" && on ? Theme.accentSoft
                                 : (onPage ? Theme.pageS2 : Theme.s2)
    readonly property color _border: variant === "primary" ? "transparent"
                                 : variant === "danger" ? Theme.crit
                                 : variant === "toggle" && on ? accentColor
                                 : (onPage ? Theme.pageBorder : Theme.border)

    contentItem: Text {
        text: b.text
        font: b.font
        color: b._fg
        opacity: b.enabled ? 1 : 0.6
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: b.radiusPx
        color: b._bg
        border.width: b.variant === "primary" ? 0 : 1
        border.color: b._border
        opacity: b.enabled ? 1 : 0.55

        Rectangle {   // hover / pressed overlay
            anchors.fill: parent
            radius: parent.radius
            color: "#7f7f7f"
            opacity: b.down ? 0.16 : (b.hovered ? 0.08 : 0)
        }
        Rectangle {   // focus ring
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: b.accentColor
            visible: b.visualFocus
        }
    }
}
