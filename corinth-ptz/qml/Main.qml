import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

ApplicationWindow {
    id: win
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: "Corinth PTZ"
    color: Theme.pageBg

    property int page: 0     // 0 live view, 1 settings, 2 joystick input

    Binding { target: Theme; property: "mode"; value: app.theme }

    // Safety: losing window focus while a key or button is held must stop the camera.
    onActiveChanged: if (!active) ptz.stopMove()

    // Full screen is the window's own business: F11 and Esc work wherever the keyboard focus is
    // (a click on any button used to take the focus away from the live view, and F11 stopped working).
    function toggleFullScreen() { visibility = (visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen) }
    Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; onActivated: win.toggleFullScreen() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; enabled: win.visibility === Window.FullScreen; onActivated: win.visibility = Window.Windowed }

    // The camera's HTTPS certificate is its own (self-signed): show its fingerprint once, then pin it.
    Connections {
        target: app
        function onCertificateChanged() { if (Object.keys(app.pendingCertificate).length > 0) certPop.open(); else certPop.close() }
    }
    Popup {
        id: certPop
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        padding: 20
        closePolicy: Popup.NoAutoClose
        readonly property bool changed: !!app.pendingCertificate.changed
        background: Card { border.color: certPop.changed ? Theme.crit : Theme.warn }
        contentItem: ColumnLayout {
            spacing: 12
            Lbl { text: certPop.changed ? "Camera certificate has CHANGED" : "Trust this camera's certificate?"; color: certPop.changed ? Theme.crit : Theme.text; font.pixelSize: 14 }
            Text {
                Layout.preferredWidth: 440; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13
                text: certPop.changed
                      ? (app.pendingCertificate.camera || "The camera") + " (" + (app.pendingCertificate.host || "") + ") is presenting a different HTTPS certificate from the one you approved. That can mean the camera was reset or replaced, or that something else on the network is answering for it. Do not trust it unless you know why."
                      : (app.pendingCertificate.camera || "The camera") + " (" + (app.pendingCertificate.host || "") + ") uses HTTPS with its own certificate, which Windows cannot check. Compare this fingerprint with the one on the camera's certificate page. If it matches, trust it: only this exact certificate will be accepted from now on."
            }
            Mono { Layout.preferredWidth: 440; wrapMode: Text.WrapAnywhere; font.pixelSize: 12; text: "SHA-256  " + (app.pendingCertificate.fingerprint || "") }
            RowLayout {
                spacing: 8
                Btn { text: "Do not trust"; variant: "ghost"; Layout.fillWidth: true; onClicked: app.rejectCertificate() }
                Btn { text: certPop.changed ? "Trust the new certificate" : "Trust this certificate"; variant: certPop.changed ? "danger" : "primary"; Layout.fillWidth: true; onClicked: app.trustCertificate() }
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: win.page

        LiveView {
            objectName: "liveView"
            isFullScreen: win.visibility === Window.FullScreen
            onOpenSettings: win.page = 1
            onOpenInput: win.page = 2
            onToggleFullscreen: win.toggleFullScreen()
        }
        SettingsPage {
            objectName: "settingsPage"
            active: win.page === 1
            onBack: win.page = 0
            onOpenInput: win.page = 2
        }
        InputPage {
            active: win.page === 2
            onBack: win.page = 0
            onOpenSettings: win.page = 1
        }
    }
}
