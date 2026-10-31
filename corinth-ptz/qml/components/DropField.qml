import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Corinth

// Label + dropdown. model is an array of strings.
ColumnLayout {
    id: df
    property string label: ""
    property var model: []
    property string value: ""
    property bool changed: false
    property bool restart: false
    signal picked(string value)
    spacing: 6

    RowLayout {
        spacing: 6
        Lbl { text: df.label }
        Dot { visible: df.changed; color: Theme.accent; size: 6 }
        Lbl { visible: df.restart; text: "· restart"; color: Theme.warn; font.pixelSize: 10 }
    }
    ComboBox {
        id: cb
        Layout.fillWidth: true
        implicitHeight: 36
        model: df.model
        currentIndex: Math.max(0, df.model.indexOf(df.value))
        font.family: Theme.uiFont
        font.pixelSize: 13
        Accessible.name: df.label
        onActivated: (i) => df.picked(df.model[i])
        contentItem: Text {
            leftPadding: 10
            text: cb.displayText
            font: cb.font
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: cb.width - width - 10
            y: (cb.height - height) / 2
            text: "▾"
            color: Theme.text2
            font.pixelSize: 12
        }
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.s2
            border.width: cb.visualFocus ? 2 : 1
            border.color: cb.visualFocus || df.changed ? Theme.accent : Theme.border
        }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: cb.width
            height: 34
            highlighted: cb.highlightedIndex === index
            contentItem: Text { text: modelData; color: Theme.text; font: cb.font; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: (highlighted || cb.currentIndex === index) ? Theme.accentSoft : "transparent"; radius: Theme.radiusInner }
        }
        popup: Popup {
            y: cb.height + 4
            width: cb.width
            padding: 4
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: cb.popup.visible ? cb.delegateModel : null
                currentIndex: cb.highlightedIndex
            }
            background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 10 }
        }
    }
}
