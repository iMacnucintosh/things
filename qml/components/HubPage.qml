import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Sylphrena" section: the extensions of the Sylphrena Hub (and what is installed for the shell).
// Rows: available · installed · update · local (put there by hand: never touched unless asked).
Item {
    id: root

    property real bottomPad: 20
    /// "sylphrena:<id>" → true while queued/running (row spinner)
    property var busyMap: ({})
    /// kind: install | remove
    signal actionRequested(string kind, string id, string name)
    /// The row was clicked: show its details
    signal openRequested(string id)
    property string selectedId: ""

    function busy(id) { return !!busyMap["sylphrena:" + id] }
    function stateLabel(state) {
        switch (state) {
        case "installed": return qsTr("Installed")
        case "update": return qsTr("Update available")
        case "local": return qsTr("Local")
        }
        return ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 16
            Layout.topMargin: 18
            Layout.bottomMargin: 10
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 10
                    StyledText {
                        text: qsTr("Sylphrena")
                        font.pixelSize: 24
                        font.weight: Font.Medium
                        color: Theme.onSurface
                    }
                    // The version of the shell that is running: the one the extensions' "requires" are checked against
                    Rectangle {
                        visible: Hub.shellVersion.length > 0
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: shellVersionText.implicitWidth + 16
                        implicitHeight: 24
                        radius: 12
                        color: Theme.surfaceContainerHigh
                        StyledText {
                            id: shellVersionText
                            anchors.centerIn: parent
                            text: "v" + Hub.shellVersion
                            font.pixelSize: 12
                            color: Theme.onSurfaceVariant
                        }
                    }
                }
                StyledText {
                    Layout.fillWidth: true
                    text: qsTr("Extensions for the Sylphrena shell")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Install from zip")
                icon: "folder_zip"
                tone: "tonal"
                enabled: Hub.tool.length > 0
                onClicked: Hub.pickZip()
            }

            IconButton {
                icon: "refresh"
                enabled: !Hub.loading
                onClicked: Hub.refresh(true)
            }
        }

        // No Sylphrena found: nothing can be installed
        Rectangle {
            visible: Hub.tool.length === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            implicitHeight: toolText.implicitHeight + 16
            radius: 12
            color: Qt.alpha(Theme.error, 0.14)
            StyledText {
                id: toolText
                anchors.fill: parent
                anchors.margins: 8
                wrapMode: Text.WordWrap
                color: Theme.onSurface
                font.pixelSize: 13
                text: qsTr("Sylphrena was not found on this computer: you can see the catalogue, but not install or remove extensions.")
            }
        }

        Rectangle {
            visible: Hub.notice.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            implicitHeight: noticeText.implicitHeight + 16
            radius: 12
            color: Theme.surfaceContainerHigh
            StyledText {
                id: noticeText
                anchors.fill: parent
                anchors.margins: 8
                wrapMode: Text.WordWrap
                color: Theme.onSurfaceVariant
                font.pixelSize: 13
                text: Hub.notice
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outlineVariant
            opacity: 0.45
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                model: Hub.items
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                topMargin: 6
                bottomMargin: root.bottomPad
                visible: count > 0

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: row
                    required property var modelData
                    readonly property string state_: modelData.state
                    readonly property bool working: root.busy(modelData.id)

                    width: ListView.view.width
                    height: 76

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        radius: 18
                        color: row.modelData.id === root.selectedId ? Qt.alpha(Theme.primaryContainer, 0.34)
                             : (hover.hovered ? Theme.surfaceContainerLow : Qt.alpha(Theme.surfaceContainerLow, 0))
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openRequested(row.modelData.id)
                    }

                    // On the whole row (not on the inset background): no gaps, and buttons keep it
                    HoverHandler { id: hover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            Layout.alignment: Qt.AlignVCenter
                            radius: 14
                            color: Theme.surfaceContainerHigh
                            MaterialIcon {
                                anchors.centerIn: parent
                                icon: row.modelData.icon
                                size: 24
                                colour: Theme.onSurfaceVariant
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                StyledText {
                                    Layout.minimumWidth: 48
                                    // Leaves room for version, chips and buttons: a narrow window elides the name
                                    Layout.maximumWidth: Math.max(80, row.width - 560)
                                    text: row.modelData.name
                                    font.pixelSize: 15
                                    font.weight: Font.Medium
                                    color: Theme.onSurface
                                    elide: Text.ElideRight
                                }
                                StyledText {
                                    // update: 1.0.0 → 1.1.0 · otherwise the installed or available version
                                    text: row.state_ === "update"
                                        ? row.modelData.installedVersion + " → " + row.modelData.version
                                        : (row.modelData.installedVersion.length > 0 ? row.modelData.installedVersion : row.modelData.version)
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 12
                                }
                                Rectangle {
                                    visible: row.state_ !== "available"
                                    implicitWidth: chipText.implicitWidth + 16
                                    implicitHeight: 22
                                    radius: 11
                                    color: row.state_ === "update" ? Theme.primaryContainer : Theme.surfaceContainerHigh
                                    StyledText {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: root.stateLabel(row.state_)
                                        font.pixelSize: 11
                                        color: row.state_ === "update" ? Theme.accentFg : Theme.onSurfaceVariant
                                    }
                                }
                                Rectangle {
                                    // Only when there is room (a narrow window keeps the essentials)
                                    visible: row.modelData.system && row.width >= 700
                                    implicitWidth: rootText.implicitWidth + 16
                                    implicitHeight: 22
                                    radius: 11
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Theme.outlineVariant
                                    StyledText {
                                        id: rootText
                                        anchors.centerIn: parent
                                        text: qsTr("Asks for a password")
                                        font.pixelSize: 11
                                        color: Theme.onSurfaceVariant
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }

                            StyledText {
                                Layout.fillWidth: true
                                text: row.modelData.description
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }

                        Spinner {
                            visible: row.working
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Install / update: only what the hub published (a local copy is left alone)
                        ActionButton {
                            visible: !row.working && (row.state_ === "available" || row.state_ === "update")
                            Layout.alignment: Qt.AlignVCenter
                            text: row.state_ === "update" ? qsTr("Update") : qsTr("Install")
                            icon: row.state_ === "update" ? "update" : "download"
                            enabled: Hub.tool.length > 0
                            onClicked: root.actionRequested("install", row.modelData.id, row.modelData.name)
                        }

                        // A local copy the hub also has: switch to the hub's, so Things can update it
                        ActionButton {
                            visible: !row.working && row.state_ === "local" && !!row.modelData.inHub
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Use the hub's copy")
                            icon: "download"
                            enabled: Hub.tool.length > 0
                            onClicked: root.actionRequested("install", row.modelData.id, row.modelData.name)
                        }

                        // Remove: a full button, or just the icon next to Actualizar
                        ActionButton {
                            visible: !row.working && (row.state_ === "installed" || row.state_ === "local")
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Remove")
                            icon: "delete"
                            tone: "tonal"
                            enabled: Hub.tool.length > 0
                            onClicked: root.actionRequested("remove", row.modelData.id, row.modelData.name)
                        }
                        IconButton {
                            visible: !row.working && row.state_ === "update"
                            Layout.alignment: Qt.AlignVCenter
                            icon: "delete"
                            enabled: Hub.tool.length > 0
                            onClicked: root.actionRequested("remove", row.modelData.id, row.modelData.name)
                        }
                    }
                }
            }

            EmptyState {
                anchors.fill: parent
                visible: list.count === 0
                icon: Hub.loading ? "hourglass_top" : "extension"
                busy: Hub.loading
                title: Hub.loading ? qsTr("Loading the catalogue…") : qsTr("There are no extensions yet")
                subtitle: Hub.loading ? "" : qsTr("When the hub publishes extensions for Sylphrena, they will show up here.")
            }
        }
    }
}
