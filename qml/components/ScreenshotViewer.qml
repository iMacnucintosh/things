import QtQuick
import QtQuick.Layouts

// Full-window screenshot viewer (lightbox). Esc / click outside closes; ←/→ browse.
Item {
    id: root
    property bool open: false
    property var shots: []
    property int index: 0
    property real progress: open ? 1 : 0
    signal closeRequested()

    anchors.fill: parent
    visible: progress > 0.01
    z: 108

    Behavior on progress { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    function show(i) { index = i; open = true }
    function step(dir) {
        if (shots.length > 1)
            index = (index + dir + shots.length) % shots.length
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.progress * 0.82
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.open
        onClicked: root.closeRequested()
    }

    Image {
        id: img
        anchors.centerIn: parent
        width: parent.width - 180
        height: parent.height - 170
        source: root.shots.length ? root.shots[root.index].full : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        opacity: root.progress * (status === Image.Ready ? 1 : 0)
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
    Spinner {
        anchors.centerIn: parent
        size: 32
        colour: "#ffffff"
        visible: root.open && img.status === Image.Loading
    }

    // Caption + counter
    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: Math.min(parent.width - 160, 760)
        spacing: 4
        opacity: root.progress
        StyledText {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.shots.length ? root.shots[root.index].caption : ""
            color: "#ffffff"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        StyledText {
            Layout.fillWidth: true
            visible: root.shots.length > 1
            text: qsTr("%1 / %2").arg(root.index + 1).arg(root.shots.length)
            color: "#ffffff"
            opacity: 0.7
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }
    }

    ArrowButton {
        visible: root.shots.length > 1
        anchors.left: parent.left
        anchors.leftMargin: 28
        anchors.verticalCenter: parent.verticalCenter
        glyph: "chevron_left"
        onClicked: root.step(-1)
    }
    ArrowButton {
        visible: root.shots.length > 1
        anchors.right: parent.right
        anchors.rightMargin: 28
        anchors.verticalCenter: parent.verticalCenter
        glyph: "chevron_right"
        onClicked: root.step(1)
    }
    ArrowButton {
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 20
        glyph: "close"
        onClicked: root.closeRequested()
    }
}
