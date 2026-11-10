import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Descubrir": featured app + rows of trending / popular / new / recently updated apps (Flathub).
Flickable {
    id: root
    property real bottomPad: 20
    signal openApp(string source, string id, string repo)
    signal installApp(string id, string name)
    signal installCatalog()

    readonly property int side: 22
    readonly property alias archRow: archRow   // (demo / tests)

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: width
    contentHeight: col.implicitHeight + 24 + bottomPad

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
        implicitWidth: 8
        contentItem: Rectangle {
            implicitWidth: 4
            radius: 2
            color: Theme.outline
            opacity: parent.active ? 0.6 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }
        background: null
    }

    function sectionTitle(id) {
        return id === "trending" ? qsTr("Tendencias")
             : id === "popular" ? qsTr("Más instaladas")
             : id === "new" ? qsTr("Novedades")
             : qsTr("Actualizadas hace poco")
    }
    function sectionSubtitle(id) {
        return id === "trending" ? qsTr("Las que más están subiendo")
             : id === "popular" ? qsTr("Las más instaladas del último mes")
             : id === "new" ? qsTr("Recién llegadas a Flathub")
             : qsTr("Con una versión nueva")
    }
    function footerFor(id) {
        return id === "new" ? "added" : id === "updated" ? "updated" : "installs"
    }

    ColumnLayout {
        id: col
        x: root.side
        y: 16
        width: root.width - root.side * 2
        spacing: 22

        // Offline / error notice
        Rectangle {
            Layout.fillWidth: true
            visible: Discover.notice.length > 0
            implicitHeight: noticeText.implicitHeight + 16
            radius: 12
            color: Theme.surfaceContainerHigh
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                MaterialIcon { icon: "cloud_off"; size: 18; colour: Theme.onSurfaceVariant }
                StyledText {
                    id: noticeText
                    Layout.fillWidth: true
                    text: Discover.notice
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }

        // First launch, nothing cached yet
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 80
            visible: !Discover.hasData && Discover.loading
            spacing: 12
            Spinner { size: 22; colour: Theme.onSurfaceVariant }
            StyledText {
                text: qsTr("Buscando novedades en Flathub…")
                color: Theme.onSurfaceVariant
                font.pixelSize: 14
            }
        }

        HeroCard {
            Layout.fillWidth: true
            visible: !!Discover.hero.id
            app: Discover.hero
            onOpenApp: root.openApp("flatpak", Discover.hero.id, "flathub")
            onInstallApp: root.installApp(Discover.hero.id, Discover.hero.name)
        }

        // Arch (pacman) applications ranked by pkgstats: needs archlinux-appstream-data
        ColumnLayout {
            Layout.fillWidth: true
            visible: ArchApps.available && ArchApps.items.length > 0
            spacing: 8

            Item {
                Layout.fillWidth: true
                implicitHeight: archTitle.implicitHeight
                ColumnLayout {
                    id: archTitle
                    anchors.left: parent.left
                    anchors.right: arrowsArch.left
                    anchors.rightMargin: 12
                    spacing: 0
                    StyledText {
                        text: qsTr("Populares en Arch")
                        font.pixelSize: 19
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: qsTr("Aplicaciones de los repositorios oficiales que más usuarios tienen instaladas")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                Row {
                    id: arrowsArch
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    ArrowButton { glyph: "chevron_left"; active: archRow.canLeft; onClicked: archRow.page(-1) }
                    ArrowButton { glyph: "chevron_right"; active: archRow.canRight; onClicked: archRow.page(1) }
                }
            }
            CardRow {
                id: archRow
                Layout.fillWidth: true
                Layout.preferredHeight: 204
                model: ArchApps.items
                delegate: AppCard {
                    required property var modelData
                    app: modelData
                    source: "pacman"
                    footer: "popularity"
                    onClicked: root.openApp("pacman", modelData.id, modelData.repo)
                }
            }
        }

        // Catalog missing: offer to install it (small package) instead of showing nothing
        Rectangle {
            Layout.fillWidth: true
            visible: !ArchApps.available
            implicitHeight: catalogRow.implicitHeight + 28
            radius: 20
            color: Theme.surfaceContainer
            border.width: 1
            border.color: Qt.alpha(Theme.outlineVariant, 0.5)
            RowLayout {
                id: catalogRow
                anchors.fill: parent
                anchors.margins: 14
                anchors.leftMargin: 18
                spacing: 14
                SourceIcon { source: "pacman"; size: 24; colour: Theme.onSurfaceVariant }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    StyledText {
                        text: qsTr("Añade las aplicaciones de Arch a Descubrir")
                        font.pixelSize: 14
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: qsTr("Instala archlinux-appstream-data (el catálogo oficial de aplicaciones de Arch, ~20 MB) para ver aquí las más populares.")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
                ActionButton {
                    text: qsTr("Instalar")
                    icon: "download"
                    tone: "tonal"
                    onClicked: root.installCatalog()
                }
            }
        }

        Repeater {
            model: Discover.sections

            delegate: ColumnLayout {
                id: sec
                required property var modelData
                Layout.fillWidth: true
                spacing: 8

                Item {
                    Layout.fillWidth: true
                    implicitHeight: secTitle.implicitHeight
                    ColumnLayout {
                        id: secTitle
                        anchors.left: parent.left
                        anchors.right: arrowsSec.left
                        anchors.rightMargin: 12
                        spacing: 0
                        StyledText {
                            text: root.sectionTitle(sec.modelData.id)
                            font.pixelSize: 19
                        }
                        StyledText {
                            Layout.fillWidth: true
                            text: root.sectionSubtitle(sec.modelData.id)
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: arrowsSec
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        ArrowButton { glyph: "chevron_left"; active: secRow.canLeft; onClicked: secRow.page(-1) }
                        ArrowButton { glyph: "chevron_right"; active: secRow.canRight; onClicked: secRow.page(1) }
                    }
                }

                CardRow {
                    id: secRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 204
                    model: sec.modelData.items
                    delegate: AppCard {
                        required property var modelData
                        app: modelData
                        footer: root.footerFor(sec.modelData.id)
                        onClicked: root.openApp("flatpak", modelData.id, "flathub")
                    }
                }
            }
        }

        StyledText {
            Layout.fillWidth: true
            visible: Discover.hasData
            text: qsTr("Aplicaciones Flatpak de Flathub · datos y capturas de flathub.org")
            color: Theme.onSurfaceVariant
            opacity: 0.7
            font.pixelSize: 11
        }
    }
}
