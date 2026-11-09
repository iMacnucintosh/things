import QtQuick
import QtQuick.Layouts

// Rail destination (shape + metrics of Seeker's SidePlace). The selection pill lives
// in the rail so it can glide between items.
Item {
    id: root
    property string symbol: ""
    property string label: ""
    property string trailing: ""
    property bool active: false
    property bool hovered: false
    property bool pressed: false
    signal clicked()

    implicitHeight: 44
    height: 44

    readonly property color labelColor: root.active ? Theme.accentFg : Theme.onSurface
    readonly property color iconColor: root.active ? Theme.accentFg : Theme.onSurfaceVariant

    StateLayer {
        z: 2
        radius: root.height / 2
        shapeMorph: true
        color: root.active ? Theme.accentFg : Theme.onSurface
        stateOpacity: 0
        onClicked: root.clicked()
        onContainsMouseChanged: root.hovered = containsMouse
        onPressedChanged: root.pressed = pressed
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 16
        spacing: 12
        z: 1

        Item {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            Layout.alignment: Qt.AlignVCenter
            MaterialIcon {
                anchors.centerIn: parent
                icon: root.symbol
                size: 22
                fill: root.active ? 1 : 0
                colour: root.iconColor
            }
        }
        StyledText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.labelColor
            elide: Text.ElideRight
        }
        StyledText {
            visible: root.trailing.length > 0
            Layout.alignment: Qt.AlignVCenter
            text: root.trailing
            color: root.labelColor
            opacity: 0.75
            font.pixelSize: 12
        }
    }
}
