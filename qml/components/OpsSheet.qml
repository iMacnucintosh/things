import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Floating activity card: current operation, progress, queue and foldable live output.
Rectangle {
    id: root
    property bool expanded: false

    readonly property var items: Ops.queue
    readonly property int pendingCount: {
        let n = 0
        for (let i = 0; i < items.length; ++i)
            if (items[i].status === "queued")
                ++n
        return n
    }
    readonly property bool anyFailed: {
        for (let i = 0; i < items.length; ++i)
            if (items[i].status === "failed")
                return true
        return false
    }
    readonly property bool allCancelled: {
        for (let i = 0; i < items.length; ++i)
            if (items[i].status !== "cancelled")
                return false
        return items.length > 0
    }
    readonly property bool working: Ops.active || Ops.running
    readonly property string stateIcon: Ops.awaitingPassword ? "lock"
        : working ? "progress_activity"
        : anyFailed ? "error"
        : allCancelled ? "cancel" : "check_circle"
    readonly property color stateColour: anyFailed && !working ? Theme.error : Theme.primary

    visible: items.length > 0
    radius: 20
    color: Theme.surfaceContainerHigh
    border.width: 1
    border.color: Theme.outlineVariant
    clip: true
    implicitHeight: 76 + (expanded ? 276 : 0) + (anyFailed && !working && Ops.hint.length ? hintBox.height + 4 : 0)

    Behavior on implicitHeight { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 76

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 10
                spacing: 14

                MaterialIcon {
                    Layout.alignment: Qt.AlignVCenter
                    icon: root.stateIcon
                    size: 26
                    fill: root.working ? 0 : 1
                    colour: root.stateColour
                    RotationAnimator on rotation {
                        running: root.working && !Ops.awaitingPassword
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite
                        onRunningChanged: if (!running) parent.rotation = 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    StyledText {
                        Layout.fillWidth: true
                        text: {
                            if (root.working)
                                return Ops.currentTitle + (root.pendingCount > 0 ? "  ·  +" + root.pendingCount + " " + qsTr("en cola") : "")
                            if (root.anyFailed)
                                return qsTr("La operación ha fallado")
                            if (root.allCancelled)
                                return qsTr("Operación cancelada")
                            return qsTr("Completado")
                        }
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: Ops.awaitingPassword ? qsTr("Esperando tu contraseña…")
                              : (root.working ? Ops.statusText : (root.anyFailed ? qsTr("Despliega la salida para ver el detalle") : ""))
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        visible: text.length > 0
                    }
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    icon: "terminal"
                    colour: root.expanded ? Theme.primary : Theme.onSurfaceVariant
                    onClicked: root.expanded = !root.expanded
                }
                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.working
                    icon: "stop_circle"
                    colour: Theme.error
                    onClicked: Ops.cancelCurrent()
                }
                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    visible: !root.working
                    icon: "close"
                    onClicked: {
                        root.expanded = false
                        Ops.dismissFinished()
                    }
                }
            }

            // Progress track
            Rectangle {
                id: track
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                height: 4
                radius: 2
                color: Theme.surfaceContainerHighest
                visible: root.working
                clip: true

                Rectangle {
                    height: parent.height
                    radius: 2
                    color: Theme.primary
                    width: Ops.progress >= 0 ? parent.width * Ops.progress : parent.width * 0.3
                    x: Ops.progress >= 0 ? 0 : sweep.x
                    Behavior on width { enabled: Ops.progress >= 0; NumberAnimation { duration: 200 } }

                    SequentialAnimation on opacity {
                        running: false
                    }
                }
                // indeterminate sweep driver
                Item {
                    id: sweep
                    property real x: 0
                    NumberAnimation on x {
                        running: root.working && Ops.progress < 0
                        from: -track.width * 0.3
                        to: track.width
                        duration: 1300
                        loops: Animation.Infinite
                    }
                }
            }
        }

        // Failure hint
        Rectangle {
            id: hintBox
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            visible: root.anyFailed && !root.working && Ops.hint.length > 0
            Layout.preferredHeight: visible ? hintText.implicitHeight + 20 : 0
            radius: 12
            color: Qt.alpha(Theme.error, 0.14)
            StyledText {
                id: hintText
                anchors.fill: parent
                anchors.margins: 10
                text: Ops.hint
                color: Theme.error
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        // Output
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            Layout.topMargin: 8
            radius: 14
            color: Theme.surfaceContainer
            visible: root.expanded || height > 20
            clip: true

            Flickable {
                id: logFlick
                anchors.fill: parent
                anchors.margins: 10
                contentWidth: width
                contentHeight: logText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    implicitWidth: 6
                    contentItem: Rectangle { implicitWidth: 4; radius: 2; color: Theme.outline; opacity: 0.5 }
                    background: null
                }

                TextEdit {
                    id: logText
                    width: logFlick.width
                    readOnly: true
                    selectByMouse: true
                    text: Ops.log
                    color: Theme.onSurfaceVariant
                    selectedTextColor: Theme.onPrimary
                    selectionColor: Theme.primary
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: TextEdit.WrapAnywhere
                    renderType: Text.NativeRendering
                    onTextChanged: Qt.callLater(function () {
                        logFlick.contentY = Math.max(0, logFlick.contentHeight - logFlick.height)
                    })
                }
            }
        }
    }
}
