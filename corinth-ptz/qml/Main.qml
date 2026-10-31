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

    StackLayout {
        anchors.fill: parent
        currentIndex: win.page

        LiveView {
            onOpenSettings: win.page = 1
            onOpenInput: win.page = 2
            onToggleFullscreen: win.visibility = (win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen)
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
