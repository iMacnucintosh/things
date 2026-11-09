import QtQuick

// Origin glyph — same mapping as Sylphrena's Updates page:
// pacman = settings, Flatpak = deployed_code, AUR = 👽 (Noto Color Emoji, 1.35× icon size).
Item {
    id: root
    property string source: "pacman"   // pacman | aur | flatpak
    property real size: 20
    property color colour: Theme.onSurfaceVariant
    property real fill: 0

    implicitWidth: size + 4
    implicitHeight: size + 4
    width: implicitWidth
    height: implicitHeight

    MaterialIcon {
        anchors.centerIn: parent
        visible: root.source !== "aur"
        icon: root.source === "flatpak" ? "deployed_code" : "settings"
        size: root.size
        fill: root.fill
        colour: root.colour
    }

    Text {
        anchors.centerIn: parent
        visible: root.source === "aur"
        text: "👽"
        font.family: "Noto Color Emoji"
        font.pixelSize: Math.round(root.size * 1.35)
        renderType: Text.NativeRendering
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
