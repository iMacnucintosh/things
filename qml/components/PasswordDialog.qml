import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Own Material You password prompt (no polkit). The secret goes straight to Ops and
// is only kept in memory — see Privilege.cpp.
Item {
    id: root
    property bool open: Ops.awaitingPassword
    property real progress: open ? 1 : 0
    property string heading: qsTr("Authorization needed")

    anchors.fill: parent
    visible: progress > 0.01
    z: 120

    Behavior on progress {
        NumberAnimation {
            duration: root.open ? 240 : 150
            easing.type: root.open ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 1.12
        }
    }

    onOpenChanged: {
        if (open) {
            pw.text = ""
            Qt.callLater(function () { pw.forceActiveFocus() })
        } else {
            pw.text = ""
        }
    }

    function submit() {
        if (pw.text.length === 0 || Ops.authBusy)
            return
        Ops.submitPassword(pw.text)
    }

    Connections {
        target: Ops
        function onPasswordStateChanged() {
            if (Ops.authError.length > 0) {
                pw.text = ""
                pw.forceActiveFocus()
                shake.restart()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.progress * 0.5
    }
    MouseArea { anchors.fill: parent; enabled: root.open }

    Rectangle {
        id: card
        anchors.centerIn: parent
        anchors.verticalCenterOffset: (1 - root.progress) * 18
        width: 400
        height: col.implicitHeight + 40
        radius: 24
        color: Theme.surfaceContainerHigh
        border.width: 1
        border.color: Theme.outlineVariant
        opacity: root.progress
        scale: 0.92 + root.progress * 0.08

        SequentialAnimation {
            id: shake
            PropertyAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: -10; duration: 50 }
            PropertyAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 10; duration: 80 }
            PropertyAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: -6; duration: 70 }
            PropertyAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 0; duration: 60 }
        }

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            RowLayout {
                spacing: 14
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 16
                    color: Theme.primaryContainer
                    MaterialIcon {
                        anchors.centerIn: parent
                        icon: "lock"
                        size: 24
                        fill: 1
                        colour: Theme.accentFg
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    StyledText {
                        Layout.fillWidth: true
                        text: root.heading
                        font.pixelSize: 17
                        elide: Text.ElideRight
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: Ops.currentTitle
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }

            StyledText {
                Layout.fillWidth: true
                text: qsTr("Enter your password to continue. It is only kept in memory for a few minutes and is never written to disk.")
                color: Theme.onSurfaceVariant
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            TextField {
                id: pw
                Layout.fillWidth: true
                echoMode: TextInput.Password
                passwordCharacter: "•"
                placeholderText: qsTr("Password")
                placeholderTextColor: Theme.onSurfaceVariant
                color: Theme.onSurface
                selectedTextColor: Theme.onPrimary
                selectionColor: Theme.primary
                font.family: Theme.uiFamily
                font.pixelSize: 15
                enabled: !Ops.authBusy
                leftPadding: 16
                rightPadding: 16
                topPadding: 12
                bottomPadding: 12
                background: Rectangle {
                    radius: 16
                    color: Theme.surfaceContainerHighest
                    border.width: 2
                    border.color: Ops.authError.length ? Theme.error : (pw.activeFocus ? Theme.primary : "transparent")
                    Behavior on border.color { ColorAnimation { duration: 140 } }
                }
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
                Keys.onEscapePressed: Ops.cancelPassword()
            }

            RowLayout {
                visible: Ops.authError.length > 0
                spacing: 8
                MaterialIcon { icon: "error"; size: 18; fill: 1; colour: Theme.error }
                StyledText {
                    Layout.fillWidth: true
                    text: Ops.authError
                    color: Theme.error
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                ActionButton {
                    text: qsTr("Cancel")
                    tone: "text"
                    onClicked: Ops.cancelPassword()
                }
                ActionButton {
                    text: qsTr("Authorize")
                    icon: "key"
                    busy: Ops.authBusy
                    enabled: pw.text.length > 0
                    onClicked: root.submit()
                }
            }
        }
    }
}
