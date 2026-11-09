import QtQuick
import QtQuick.Layouts

// Filter chip: pill, tonal when selected (same accent rule as the rail selection).
Item {
    id: root
    property string label: ""
    property string source: ""     // optional leading origin glyph
    property string icon: ""       // optional leading Material glyph
    property string count: ""
    property bool selected: false
    signal clicked()

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 28
    height: implicitHeight
    width: implicitWidth

    readonly property color fg: selected ? Theme.accentFg : Theme.onSurfaceVariant

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: height / 2
        color: root.selected ? Theme.primaryContainer : "transparent"
        border.width: root.selected ? 0 : 1
        border.color: Theme.outlineVariant
        scale: layer.pressed ? 0.96 : 1
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8

        SourceIcon {
            visible: root.source.length > 0
            source: root.source
            size: 16
            colour: root.fg
            Layout.alignment: Qt.AlignVCenter
        }
        MaterialIcon {
            visible: root.icon.length > 0
            icon: root.icon
            size: 18
            colour: root.fg
            Layout.alignment: Qt.AlignVCenter
        }
        StyledText {
            text: root.label
            color: root.fg
            font.pixelSize: 13
            Layout.alignment: Qt.AlignVCenter
        }
        StyledText {
            visible: root.count.length > 0
            text: root.count
            color: root.fg
            opacity: 0.7
            font.pixelSize: 12
            Layout.alignment: Qt.AlignVCenter
        }
    }

    StateLayer {
        id: layer
        radius: bg.radius
        color: root.selected ? Theme.accentFg : Theme.onSurface
        onClicked: root.clicked()
    }
}
