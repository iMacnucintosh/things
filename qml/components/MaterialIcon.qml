import QtQuick

// Material Symbols Rounded — same family Sylphrena uses for chrome icons.
Text {
    id: root
    property string icon: ""
    property real size: 20
    property real fill: 0
    property color colour: Theme.onSurface

    // Fixed box so a failed ligature cannot spill into neighbours.
    width: size + 4
    height: size + 4
    clip: true

    text: icon
    color: colour
    renderType: Text.NativeRendering
    textFormat: Text.PlainText
    font.family: Theme.iconFamily
    font.pixelSize: size
    font.weight: Font.Normal
    font.features: { "liga": 1, "clig": 1 }
    font.variableAxes: {
        "FILL": root.fill,
        "wght": 400,
        "GRAD": Theme.mode === "light" ? 0 : -25,
        "opsz": Math.min(48, Math.max(20, size))
    }
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideNone
    wrapMode: Text.NoWrap

    Behavior on fill {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }
    Behavior on color {
        ColorAnimation { duration: 160 }
    }
}
