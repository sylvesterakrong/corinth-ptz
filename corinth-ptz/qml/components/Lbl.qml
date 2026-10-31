import QtQuick
import Corinth

// Uppercase condensed label, used for section and field labels only.
Text {
    property bool onPage: false
    font.family: Theme.labelFont
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 1.3
    font.pixelSize: 12
    font.weight: Font.DemiBold
    color: onPage ? Theme.pageText2 : Theme.text2
    elide: Text.ElideRight
}
