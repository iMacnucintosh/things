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

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    MaterialIcon {
                        anchors.centerIn: parent
                        visible: !(root.working && !Ops.awaitingPassword)
                        icon: root.stateIcon === "progress_activity" ? "lock" : root.stateIcon
                        size: 26
                        fill: 1
                        colour: root.stateColour
                    }
                    Spinner {
                        anchors.centerIn: parent
                        visible: root.working && !Ops.awaitingPassword
                        size: 26
                        colour: root.stateColour
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
                                return Ops.currentTitle + (root.pendingCount > 0 ? "  ·  +" + root.pendingCount + " " + qsTr("queued") : "")
                            if (root.anyFailed)
                                return qsTr("The operation has failed")
                            if (root.allCancelled)
                                return qsTr("Operation cancelled")
                            if (items.length === 1)
                                return (items[0].kind === "install" ? qsTr("%1 installed") : qsTr("%1 removed")).arg(items[0].name)
                            return qsTr("%1 operations completed").arg(items.length)
                        }
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: Ops.awaitingPassword ? qsTr("Waiting for your password…")
                              : (root.working ? Ops.statusText : (root.anyFailed ? qsTr("Expand the output to see the details") : ""))
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

                property real sweepX: 0
                NumberAnimation on sweepX {
                    running: root.working && Ops.progress < 0
                    from: -track.width * 0.3
                    to: track.width
                    duration: 1300
                    loops: Animation.Infinite
                }

                Rectangle {
                    height: parent.height
                    radius: 2
                    color: Theme.primary
                    width: Ops.progress >= 0 ? parent.width * Ops.progress : parent.width * 0.3
                    x: Ops.progress >= 0 ? 0 : track.sweepX
                    Behavior on width { enabled: Ops.progress >= 0; NumberAnimation { duration: 200 } }
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
