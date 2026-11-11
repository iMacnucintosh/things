import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Install / remove confirmation with a real preview (pacman --print, flatpak remote-info).
Item {
    id: root
    property bool open: false
    property real progress: open ? 1 : 0
    property string kind: "install"      // install | remove
    property string source: "pacman"
    property string pkgId: ""
    property string pkgName: ""
    property string repo: ""
    property bool loading: false
    property bool ok: true
    property string error: ""
    property string note: ""
    property var packages: []
    property real total: 0
    property bool deleteData: false      // Flatpak removal: also wipe ~/.var/app/<id>
    signal confirmed()

    anchors.fill: parent
    visible: progress > 0.01
    z: 105

    readonly property bool removing: kind === "remove"
    readonly property bool canConfirm: !loading && ok

    Behavior on progress {
        NumberAnimation {
            duration: root.open ? 240 : 150
            easing.type: root.open ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 1.12
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.progress * 0.45
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.open
        onClicked: root.open = false
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        anchors.verticalCenterOffset: (1 - root.progress) * 16
        width: 460
        height: Math.min(parent.height - 48, content.implicitHeight + 40)
        radius: 20
        color: Theme.surfaceContainerHigh
        border.width: 1
        border.color: Theme.outlineVariant
        opacity: root.progress
        scale: 0.94 + root.progress * 0.06
        clip: true

        MouseArea { anchors.fill: parent; z: -1; onClicked: {} }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: 14
                    color: root.removing ? Qt.alpha(Theme.error, 0.14) : Theme.surfaceContainer
                    SourceIcon {
                        anchors.centerIn: parent
                        source: root.source
                        size: 22
                        colour: root.removing ? Theme.error : Theme.onSurfaceVariant
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    StyledText {
                        Layout.fillWidth: true
                        text: (root.removing ? qsTr("Remove %1?") : qsTr("Install %1?")).arg(root.pkgName)
                        font.pixelSize: 17
                        elide: Text.ElideRight
                    }
                    StyledText {
                        text: root.source === "aur" ? "AUR" : (root.source === "flatpak" ? "Flatpak · " + root.repo : "pacman · " + root.repo)
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                    }
                }
            }

            // Loading
            RowLayout {
                visible: root.loading
                spacing: 10
                Spinner {
                    size: 20
                    colour: Theme.onSurfaceVariant
                }
                StyledText {
                    text: qsTr("Working out the changes…")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 13
                }
            }

            // Error (e.g. "breaks dependency")
            Rectangle {
                Layout.fillWidth: true
                visible: !root.loading && !root.ok
                implicitHeight: errText.implicitHeight + 20
                radius: 12
                color: Qt.alpha(Theme.error, 0.14)
                StyledText {
                    id: errText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.error
                    color: Theme.error
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }

            // Summary line
            StyledText {
                Layout.fillWidth: true
                visible: !root.loading && root.ok && root.packages.length > 0 && root.source !== "flatpak" && !(root.source === "aur" && !root.removing)
                text: root.removing
                      ? (root.packages.length === 1 ? qsTr("This package will be removed:")
                                                     : qsTr("%1 packages will be removed (the package and the dependencies nobody needs any more):").arg(root.packages.length))
                      : (root.packages.length === 1 ? qsTr("1 package will be installed")
                                                     : qsTr("%1 packages will be installed (with their dependencies)").arg(root.packages.length))
                        + (root.total > 0 ? " · " + Things.formatSize(root.total) : "")
                color: Theme.onSurfaceVariant
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            // Package list
            Rectangle {
                Layout.fillWidth: true
                visible: !root.loading && root.ok && root.packages.length > 1
                Layout.preferredHeight: Math.min(190, root.packages.length * 28 + 16)
                radius: 14
                color: Theme.surfaceContainer
                clip: true
                ListView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    model: root.packages
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Item {
                        id: pr
                        required property var modelData
                        width: ListView.view.width
                        height: 28
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 8
                            StyledText {
                                Layout.fillWidth: true
                                text: pr.modelData.name
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            StyledText {
                                text: pr.modelData.version || ""
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 12
                            }
                            StyledText {
                                visible: pr.modelData.size > 0
                                text: Things.formatSize(pr.modelData.size)
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 12
                                Layout.preferredWidth: 70
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {
                        implicitWidth: 6
                        contentItem: Rectangle { implicitWidth: 4; radius: 2; color: Theme.outline; opacity: 0.5 }
                        background: null
                    }
                }
            }

            StyledText {
                Layout.fillWidth: true
                visible: !root.loading && root.ok && root.note.length > 0
                text: root.note
                color: Theme.onSurfaceVariant
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            // Flatpak removal: runtimes are cleaned up, user data only if asked
            StyledText {
                Layout.fillWidth: true
                visible: !root.loading && root.ok && root.removing && root.source === "flatpak"
                text: qsTr("The runtimes no other app uses will also be removed.")
                color: Theme.onSurfaceVariant
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Item {
                Layout.fillWidth: true
                visible: !root.loading && root.ok && root.removing && (root.source === "flatpak" || root.source === "sylphrena")
                implicitHeight: dataRow.implicitHeight + 8

                RowLayout {
                    id: dataRow
                    anchors.fill: parent
                    spacing: 8
                    MaterialIcon {
                        icon: root.deleteData ? "check_box" : "check_box_outline_blank"
                        size: 20
                        colour: root.deleteData ? Theme.error : Theme.onSurfaceVariant
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: root.source === "sylphrena" ? qsTr("Also delete its settings and data (this cannot be undone)") : qsTr("Also delete the app's data (this cannot be undone)")
                        color: root.deleteData ? Theme.error : Theme.onSurface
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.deleteData = !root.deleteData
                }
            }

            // Permission hint
            RowLayout {
                visible: !root.loading && root.ok
                spacing: 8
                MaterialIcon {
                    icon: "lock"
                    size: 16
                    colour: Theme.onSurfaceVariant
                    opacity: 0.8
                }
                StyledText {
                    Layout.fillWidth: true
                    text: qsTr("It will ask for your administrator password if needed.")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 12
                    opacity: 0.8
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: 4
                spacing: 8
                ActionButton {
                    text: qsTr("Cancel")
                    tone: "text"
                    onClicked: root.open = false
                }
                ActionButton {
                    text: root.removing ? qsTr("Remove", "package") : qsTr("Install")
                    icon: root.removing ? "delete" : "download"
                    tone: root.removing ? "error" : "primary"
                    enabled: root.canConfirm
                    onClicked: {
                        root.open = false
                        root.confirmed()
                    }
                }
            }
        }
    }
}
