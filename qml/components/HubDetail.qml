import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right-hand drawer of an extension of the Sylphrena Hub: what it is, what it needs, what it touches on
// this computer and what changed. The texts come from the hub (store/<id>.json); the rest from its row.
Rectangle {
    id: root

    /// The row of the list (id, name, icon, version, state, needs, optional, requires, system, data…)
    property var row: ({})
    /// What the hub tells about it (Hub.details); empty while loading or if it has none
    property var details: ({})
    property bool loading: false
    property bool busy: false
    signal closeRequested()
    signal screenshotRequested(int index)
    signal installRequested()
    signal removeRequested()

    readonly property string state_: row.state || "available"
    readonly property bool hasDetails: !!details.ok
    readonly property var shots: hasDetails ? (details.screenshots || []) : []
    readonly property var needs: row.needs || []
    readonly property var optional: row.optional || []
    readonly property var touchedData: row.data || []

    function stateLabel(s) {
        return s === "installed" ? qsTr("Installed") : s === "update" ? qsTr("Update available") : s === "local" ? qsTr("Local") : ""
    }
    function linkLabel(k) { return k === "source" ? qsTr("Source code") : k === "issues" ? qsTr("Report a bug") : qsTr("Web") }
    function linkIcon(k) { return k === "source" ? "code" : k === "issues" ? "bug_report" : "public" }

    color: Theme.surfaceContainerLow
    clip: true

    component SectionTitle: StyledText {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: Theme.onSurfaceVariant
        font.pixelSize: 12
        font.weight: Font.Medium
    }
    component Bullet: RowLayout {
        property alias text: label.text
        property string icon: "circle"
        Layout.fillWidth: true
        spacing: 10
        MaterialIcon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            icon: parent.icon
            size: 16
            colour: Theme.onSurfaceVariant
        }
        StyledText {
            id: label
            Layout.fillWidth: true
            color: Theme.onSurface
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.preferredHeight: 44
            StyledText {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                text: qsTr("Details")
                color: Theme.onSurfaceVariant
                font.pixelSize: 13
            }
            IconButton {
                icon: "close"
                onClicked: root.closeRequested()
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                implicitWidth: 6
                contentItem: Rectangle {
                    implicitWidth: 4
                    radius: 2
                    color: Theme.outline
                    opacity: parent.active ? 0.55 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }
                background: null
            }

            ColumnLayout {
                id: body
                width: flick.width - 40
                x: 20
                spacing: 14

                // Identity
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        Layout.alignment: Qt.AlignTop
                        radius: 20
                        color: Theme.surfaceContainerHigh
                        MaterialIcon {
                            anchors.centerIn: parent
                            icon: root.row.icon || "extension"
                            size: 32
                            colour: Theme.onSurfaceVariant
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        StyledText {
                            Layout.fillWidth: true
                            text: root.row.name || ""
                            color: Theme.onSurface
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            spacing: 8
                            StyledText {
                                text: root.state_ === "update" ? (root.row.installedVersion + " → " + root.row.version)
                                                               : (root.row.installedVersion || root.row.version || "")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 12
                            }
                            Rectangle {
                                visible: root.state_ !== "available"
                                implicitWidth: chip.implicitWidth + 16
                                implicitHeight: 22
                                radius: 11
                                color: root.state_ === "update" ? Theme.primaryContainer : Theme.surfaceContainerHigh
                                StyledText {
                                    id: chip
                                    anchors.centerIn: parent
                                    text: root.stateLabel(root.state_)
                                    font.pixelSize: 11
                                    color: root.state_ === "update" ? Theme.accentFg : Theme.onSurfaceVariant
                                }
                            }
                        }
                        StyledText {
                            Layout.fillWidth: true
                            text: root.row.description || ""
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // Actions
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        visible: !root.busy && (root.state_ === "available" || root.state_ === "update")
                        text: root.state_ === "update" ? qsTr("Update") : qsTr("Install")
                        icon: root.state_ === "update" ? "update" : "download"
                        enabled: Hub.tool.length > 0
                        onClicked: root.installRequested()
                    }
                    ActionButton {
                        visible: !root.busy && root.state_ !== "available"
                        text: qsTr("Remove")
                        icon: "delete"
                        tone: "tonal"
                        enabled: Hub.tool.length > 0
                        onClicked: root.removeRequested()
                    }
                    Spinner {
                        visible: root.busy
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                    }
                    Item { Layout.fillWidth: true }
                }

                ScreenshotGallery {
                    Layout.fillWidth: true
                    visible: root.shots.length > 0
                    fit: true
                    shots: root.shots
                    onExpandRequested: (i) => root.screenshotRequested(i)
                }

                StyledText {
                    Layout.fillWidth: true
                    visible: root.loading
                    text: qsTr("Loading details")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 13
                }

                // About
                StyledText {
                    Layout.fillWidth: true
                    visible: root.hasDetails && (root.details.about || "").length > 0
                    text: root.details.about || ""
                    color: Theme.onSurface
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    lineHeight: 1.15
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.hasDetails && (root.details.highlights || []).length > 0
                    Repeater {
                        model: root.details.highlights || []
                        Bullet {
                            required property string modelData
                            text: modelData
                            icon: "check"
                        }
                    }
                }

                // What it needs
                SectionTitle {
                    visible: !!root.row.requires || root.needs.length > 0 || root.optional.length > 0
                    text: qsTr("Requirements")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !!root.row.requires || root.needs.length > 0 || root.optional.length > 0
                    Bullet {
                        visible: !!root.row.requires
                        text: qsTr("Sylphrena %1").arg(root.row.requires || "")
                        icon: "deployed_code"
                    }
                    Repeater {
                        model: root.needs
                        Bullet {
                            required property string modelData
                            text: modelData
                            icon: "inventory_2"
                        }
                    }
                    Repeater {
                        model: root.optional
                        Bullet {
                            required property string modelData
                            text: qsTr("Optional: %1").arg(modelData)
                            icon: "add_circle"
                        }
                    }
                }

                // What it touches on this computer (from its manifest: nothing the author has to write)
                SectionTitle { text: qsTr("What it touches on your computer") }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Bullet {
                        text: qsTr("It runs inside the shell, with the same access as your user.")
                        icon: "terminal"
                    }
                    Bullet {
                        visible: !!root.row.system
                        text: qsTr("Asks for the administrator password to install system pieces.")
                        icon: "admin_panel_settings"
                    }
                    Repeater {
                        model: root.touchedData
                        Bullet {
                            required property string modelData
                            text: qsTr("Stores data in %1").arg(modelData.replace("state:", "~/.local/state/sylphrena/").replace("config:", "~/.config/sylphrena/"))
                            icon: "database"
                        }
                    }
                    Bullet {
                        text: qsTr("Its settings go in ~/.config/sylphrena/extensions/%1.json").arg(root.row.id || "")
                        icon: "tune"
                    }
                }

                // Changelog
                SectionTitle {
                    visible: root.hasDetails && (root.details.changelog || []).length > 0
                    text: qsTr("What's new")
                }
                Repeater {
                    model: root.hasDetails ? (root.details.changelog || []) : []
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            StyledText {
                                text: modelData.version
                                color: Theme.onSurface
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                            StyledText {
                                text: modelData.date
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 12
                            }
                            Rectangle {
                                visible: modelData.version === root.row.installedVersion
                                implicitWidth: nowText.implicitWidth + 14
                                implicitHeight: 20
                                radius: 10
                                color: Theme.surfaceContainerHigh
                                StyledText {
                                    id: nowText
                                    anchors.centerIn: parent
                                    text: qsTr("Installed")
                                    font.pixelSize: 11
                                    color: Theme.onSurfaceVariant
                                }
                            }
                        }
                        Repeater {
                            model: modelData.changes
                            Bullet {
                                required property string modelData
                                text: modelData
                                icon: "chevron_right"
                            }
                        }
                    }
                }

                // Links, author and license
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 8
                    visible: root.hasDetails
                    Repeater {
                        model: root.hasDetails ? Object.keys(root.details.links || {}) : []
                        ActionButton {
                            required property string modelData
                            text: root.linkLabel(modelData)
                            icon: root.linkIcon(modelData)
                            tone: "tonal"
                            onClicked: Qt.openUrlExternally(root.details.links[modelData])
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                StyledText {
                    Layout.fillWidth: true
                    visible: root.hasDetails && (root.details.author || "").length > 0
                    text: qsTr("%1 · licence %2").arg(root.details.author || "").arg(root.details.license || "")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 12
                }
            }
        }
    }
}
