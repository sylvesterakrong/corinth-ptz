import QtQuick
import Corinth

// Monospace readout text (azimuth, zoom, speeds, stream stats).
Text {
    property bool onPage: false
    font.family: Theme.monoFont
    font.pixelSize: 12
    color: onPage ? Theme.pageText : Theme.text
}
