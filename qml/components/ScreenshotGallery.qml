import QtQuick
import QtQuick.Layouts

// Screenshot carousel for the detail drawer: image, arrows, caption and dots. Click to enlarge.
ColumnLayout {
    id: root
    property var shots: []
    property int current: 0
    signal expandRequested(int index)

    spacing: 8
    onShotsChanged: current = 0

    function step(dir) {
        if (shots.length > 1)
            current = (current + dir + shots.length) % shots.length
    }

    Rectangle {
        id: frame
        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(width * 0.62)
        radius: 16
        color: Theme.surfaceContainerHigh

        Image {
            id: img
            anchors.fill: parent
            source: root.shots.length ? root.shots[root.current].url : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }
        RoundCorners {
            anchors.fill: parent
            radius: frame.radius
            colour: Theme.surfaceContainerLow
            visible: img.status === Image.Ready
        }
        Spinner {
            anchors.centerIn: parent
            size: 26
            colour: Theme.onSurfaceVariant
            visible: img.status === Image.Loading
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expandRequested(root.current)
        }
        ArrowButton {
            visible: root.shots.length > 1
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: "chevron_left"
            onClicked: root.step(-1)
        }
        ArrowButton {
            visible: root.shots.length > 1
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: "chevron_right"
            onClicked: root.step(1)
        }
    }

    StyledText {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.shots.length ? root.shots[root.current].caption : ""
        color: Theme.onSurfaceVariant
        font.pixelSize: 12
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }

    Row {
        Layout.alignment: Qt.AlignHCenter
        visible: root.shots.length > 1
        spacing: 6
        Repeater {
            model: root.shots.length
            delegate: Rectangle {
                required property int index
                width: index === root.current ? 18 : 7
                height: 7
                radius: 3.5
                color: index === root.current ? Theme.primary : Theme.outlineVariant
                Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 160 } }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.current = index
                }
            }
        }
    }
}
