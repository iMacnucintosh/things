import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Large pill search field: leading glyph (spinner while searching), clear button.
Item {
    id: root
    property alias text: field.text
    property string placeholder: ""
    property bool busy: false
    readonly property bool focused: field.activeFocus
    signal accepted()
    signal downPressed()

    implicitHeight: 48
    height: implicitHeight

    function focusAll() {
        field.forceActiveFocus()
        field.selectAll()
    }
    function focusField() { field.forceActiveFocus() }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: height / 2
        color: field.activeFocus ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
        border.width: 2
        border.color: field.activeFocus ? Theme.primary : "transparent"
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    MaterialIcon {
        id: lead
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        icon: root.busy ? "progress_activity" : "search"
        size: 22
        colour: field.activeFocus ? Theme.primary : Theme.onSurfaceVariant
        RotationAnimator on rotation {
            running: root.busy
            from: 0; to: 360; duration: 900; loops: Animation.Infinite
            onRunningChanged: if (!running) lead.rotation = 0
        }
    }

    TextField {
        id: field
        anchors.left: lead.right
        anchors.leftMargin: 8
        anchors.right: clearBtn.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 8
        color: Theme.onSurface
        selectedTextColor: Theme.onPrimary
        selectionColor: Theme.primary
        placeholderText: root.placeholder
        placeholderTextColor: Theme.onSurfaceVariant
        font.family: Theme.uiFamily
        font.pixelSize: 15
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: 4
        rightPadding: 4
        topPadding: 0
        bottomPadding: 0
        selectByMouse: true
        background: null
        Keys.onDownPressed: root.downPressed()
        onAccepted: root.accepted()
    }

    IconButton {
        id: clearBtn
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        size: 36
        iconSize: 20
        icon: "close"
        opacity: field.text.length > 0 ? 1 : 0
        enabled: field.text.length > 0
        onClicked: {
            field.clear()
            field.forceActiveFocus()
        }
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
