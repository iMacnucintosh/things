import QtQuick

// Match Sylphrena bar/panel labels: Google Sans Flex Medium + ROND 25 + opsz.
// (Places / ActiveWindow read heavier than body Normal — use Medium.)
Text {
    id: root

    renderType: Text.NativeRendering
    textFormat: Text.PlainText
    color: Theme.onSurface
    font.family: Theme.uiFamily
    font.pixelSize: 14
    font.weight: Font.Medium
    font.hintingPreference: Font.PreferFullHinting
    // Avoid reading font.* inside variableAxes (binding loop).
    font.variableAxes: ({
        "opsz": 14,
        "wght": 500,
        "ROND": 25,
        "GRAD": 0,
        "wdth": 100
    })
}
