import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property string icon: ""       // Material Symbols name
    property color colour: Theme.onSurfaceVariant
    property color bg: "transparent"
    property real radius: height / 2
    property int size: 40
    property int iconSize: 22
    signal clicked()

    // Collapse in RowLayout when hidden (visible:false alone still reserves space).
    Layout.preferredWidth: visible ? root.size : 0
    Layout.preferredHeight: visible ? root.size : 0
    Layout.maximumWidth: visible ? root.size : 0
    implicitWidth: visible ? root.size : 0
    implicitHeight: visible ? root.size : 0
    width: visible ? root.size : 0
    height: visible ? root.size : 0
    opacity: enabled ? 1 : 0.38
    clip: !visible

    Rectangle {
        id: chrome
        anchors.centerIn: parent
        width: parent.width + (layer.pressed ? 8 : 0)
        height: width
        radius: layer.pressed ? 10 : width / 2
        color: root.bg
        scale: layer.pressed ? 0.94 : 1

        Behavior on width {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
        Behavior on radius {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: 160; easing.type: Easing.OutBack }
        }
    }

    MaterialIcon {
        anchors.centerIn: parent
        z: 1
        icon: root.icon
        colour: root.colour
        size: root.iconSize
    }

    StateLayer {
        id: layer
        z: 2
        radius: chrome.radius
        shapeMorph: true
        color: root.colour
        disabled: !root.enabled
        onClicked: root.clicked()
    }
}
