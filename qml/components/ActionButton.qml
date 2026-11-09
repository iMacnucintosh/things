import QtQuick
import QtQuick.Layouts

// Pill text button. tone: primary | tonal | error | text
Item {
    id: root
    property string text: ""
    property string icon: ""
    property string tone: "primary"
    property bool busy: false
    signal clicked()

    implicitHeight: 40
    implicitWidth: row.implicitWidth + 32
    height: implicitHeight
    width: implicitWidth
    opacity: enabled ? 1 : 0.38

    readonly property color bgColor: tone === "primary" ? Theme.primary
        : tone === "error" ? Theme.error
        : tone === "tonal" ? Theme.secondaryContainer : "transparent"
    readonly property color fgColor: tone === "primary" ? Theme.onPrimary
        : tone === "error" ? Theme.onError
        : tone === "tonal" ? Theme.onSecondaryContainer : Theme.primary

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: layer.pressed ? 12 : height / 2
        color: root.bgColor
        scale: layer.pressed ? 0.97 : 1
        Behavior on radius { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 160 } }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8
        MaterialIcon {
            visible: root.icon.length > 0 || root.busy
            icon: root.busy ? "progress_activity" : root.icon
            size: 20
            colour: root.fgColor
            Layout.alignment: Qt.AlignVCenter
            RotationAnimator on rotation {
                running: root.busy
                from: 0; to: 360; duration: 900; loops: Animation.Infinite
            }
        }
        StyledText {
            text: root.text
            color: root.fgColor
            font.pixelSize: 14
            Layout.alignment: Qt.AlignVCenter
        }
    }

    StateLayer {
        id: layer
        radius: bg.radius
        shapeMorph: true
        color: root.fgColor
        disabled: !root.enabled || root.busy
        onClicked: root.clicked()
    }
}
