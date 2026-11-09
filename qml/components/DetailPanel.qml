import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right-hand package detail drawer.
Rectangle {
    id: root
    property var detail: ({})
    property bool loading: false
    property bool busy: false
    property bool filesOpen: false
    signal closeRequested()
    signal installRequested()
    signal removeRequested()

    readonly property string source: detail.source || "pacman"
    readonly property bool installed: !!detail.installed
    readonly property string iconName: detail.id ? Things.iconFor(source, detail.id) : ""
    readonly property var fields: detail.fields || []

    color: Theme.surfaceContainerLow
    clip: true

    onDetailChanged: if (!detail.canFiles) filesOpen = false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.preferredHeight: 44
            StyledText {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                text: qsTr("Detalles")
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
                        Image {
                            anchors.centerIn: parent
                            width: 48
                            height: 48
                            visible: root.iconName.length > 0
                            source: root.iconName.length ? "image://theme/" + root.iconName : ""
                            sourceSize: Qt.size(96, 96)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        SourceIcon {
                            anchors.centerIn: parent
                            visible: root.iconName.length === 0
                            source: root.source
                            size: 30
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        StyledText {
                            Layout.fillWidth: true
                            text: root.detail.name || ""
                            font.pixelSize: 20
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        StyledText {
                            Layout.fillWidth: true
                            text: root.detail.version || ""
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            spacing: 6
                            SourceIcon {
                                source: root.source
                                size: 16
                                Layout.alignment: Qt.AlignVCenter
                            }
                            StyledText {
                                text: root.source === "aur" ? "AUR"
                                      : (root.source === "flatpak" ? "Flatpak · " + (root.detail.repo || "")
                                                                   : "pacman · " + (root.detail.repo || ""))
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }
                }

                StyledText {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.detail.description || ""
                    color: Theme.onSurface
                    font.pixelSize: 14
                    font.weight: Font.Normal
                    font.variableAxes: ({ "opsz": 14, "wght": 400, "ROND": 25, "GRAD": 0, "wdth": 100 })
                    wrapMode: Text.Wrap
                }

                // Status + actions
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ActionButton {
                        visible: !root.installed
                        text: qsTr("Instalar")
                        icon: "download"
                        busy: root.busy
                        enabled: !root.loading
                        onClicked: root.installRequested()
                    }
                    ActionButton {
                        visible: root.installed
                        text: qsTr("Eliminar")
                        icon: "delete"
                        tone: "error"
                        busy: root.busy
                        onClicked: root.removeRequested()
                    }
                    RowLayout {
                        visible: root.installed
                        spacing: 6
                        MaterialIcon {
                            icon: "check_circle"
                            size: 20
                            fill: 1
                            colour: Theme.primary
                        }
                        StyledText {
                            text: qsTr("Instalado") + (root.detail.installedVersion ? " · " + root.detail.installedVersion : "")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                // AUR warnings
                Rectangle {
                    Layout.fillWidth: true
                    visible: !!root.detail.outOfDate
                    implicitHeight: oodRow.implicitHeight + 20
                    radius: 14
                    color: Qt.alpha(Theme.error, 0.14)
                    RowLayout {
                        id: oodRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        MaterialIcon { icon: "warning"; size: 20; colour: Theme.error }
                        StyledText {
                            Layout.fillWidth: true
                            text: qsTr("Este paquete está marcado como desactualizado en AUR.")
                            color: Theme.error
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // Loading / error
                RowLayout {
                    visible: root.loading
                    spacing: 10
                    Spinner {
                        size: 20
                        colour: Theme.onSurfaceVariant
                    }
                    StyledText {
                        text: qsTr("Cargando información…")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 13
                    }
                }
                StyledText {
                    Layout.fillWidth: true
                    visible: !!root.detail.error
                    text: root.detail.error || ""
                    color: Theme.error
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }

                // Key / value sections
                Repeater {
                    model: root.fields
                    delegate: ColumnLayout {
                        id: fieldCol
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4

                        StyledText {
                            text: fieldCol.modelData.label
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                            opacity: 0.85
                        }
                        // plain text
                        StyledText {
                            Layout.fillWidth: true
                            visible: fieldCol.modelData.kind === "text"
                            text: fieldCol.modelData.value
                            font.pixelSize: 13
                            font.weight: Font.Normal
                            font.variableAxes: ({ "opsz": 14, "wght": 400, "ROND": 25, "GRAD": 0, "wdth": 100 })
                            wrapMode: Text.Wrap
                        }
                        // link
                        StyledText {
                            Layout.fillWidth: true
                            visible: fieldCol.modelData.kind === "link"
                            text: fieldCol.modelData.value
                            color: Theme.primary
                            font.pixelSize: 13
                            wrapMode: Text.WrapAnywhere
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const u = fieldCol.modelData.value
                                    if (u.indexOf("https://") === 0 || u.indexOf("http://") === 0)
                                        Qt.openUrlExternally(u)
                                }
                            }
                        }
                        // list → wrapped tonal tags
                        Flow {
                            Layout.fillWidth: true
                            visible: fieldCol.modelData.kind === "list"
                            spacing: 6
                            Repeater {
                                model: fieldCol.modelData.kind === "list"
                                       ? (fieldCol.modelData.items || []).slice(0, 40) : []
                                delegate: Rectangle {
                                    id: tag
                                    required property string modelData
                                    height: tagText.implicitHeight + 10
                                    width: Math.min(tagText.implicitWidth + 20, body.width)
                                    radius: 10
                                    color: Theme.surfaceContainerHigh
                                    StyledText {
                                        id: tagText
                                        anchors.centerIn: parent
                                        width: Math.min(implicitWidth, body.width - 20)
                                        text: tag.modelData
                                        font.pixelSize: 12
                                        font.weight: Font.Normal
                                        font.variableAxes: ({ "opsz": 14, "wght": 400, "ROND": 25, "GRAD": 0, "wdth": 100 })
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Rectangle {
                                visible: fieldCol.modelData.kind === "list" && (fieldCol.modelData.items || []).length > 40
                                height: 28
                                width: moreText.implicitWidth + 20
                                radius: 10
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.outlineVariant
                                StyledText {
                                    id: moreText
                                    anchors.centerIn: parent
                                    text: "+" + ((fieldCol.modelData.items || []).length - 40)
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                // Installed files (pacman / AUR)
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !!root.detail.canFiles
                    spacing: 8
                    ActionButton {
                        text: root.filesOpen ? qsTr("Ocultar archivos") : qsTr("Ver archivos instalados")
                        icon: root.filesOpen ? "expand_less" : "folder_open"
                        tone: "tonal"
                        onClicked: {
                            root.filesOpen = !root.filesOpen
                            if (root.filesOpen)
                                Things.loadFiles(root.detail.source, root.detail.id)
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        visible: root.filesOpen
                        Layout.preferredHeight: 240
                        radius: 14
                        color: Theme.surfaceContainer
                        clip: true

                        StyledText {
                            anchors.centerIn: parent
                            visible: Things.detailFilesLoading
                            text: qsTr("Leyendo archivos…")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 13
                        }
                        ListView {
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            model: Things.detailFiles
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Text {
                                required property string modelData
                                width: ListView.view.width
                                text: modelData
                                color: Theme.onSurfaceVariant
                                font.family: "monospace"
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                height: 18
                                renderType: Text.NativeRendering
                            }
                            ScrollBar.vertical: ScrollBar {
                                implicitWidth: 6
                                contentItem: Rectangle { implicitWidth: 4; radius: 2; color: Theme.outline; opacity: 0.5 }
                                background: null
                            }
                        }
                    }
                }
            }
        }
    }
}
