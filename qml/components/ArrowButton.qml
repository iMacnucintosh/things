import QtQuick

// Small round chevron button for row headers.
Rectangle {
    id: root
    property string glyph: "chevron_right"
    property bool active: true
    signal clicked()

    implicitWidth: 36
    implicitHeight: 36
    radius: 18
    color: area.containsMouse && active ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
    border.width: 1
    border.color: Qt.alpha(Theme.outlineVariant, 0.7)
    opacity: active ? 1 : 0.38
    scale: area.pressed && active ? 0.92 : 1
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: 120 } }

    MaterialIcon {
        anchors.centerIn: parent
        icon: root.glyph
        size: 22
        colour: Theme.onSurface
    }
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: root.active
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
