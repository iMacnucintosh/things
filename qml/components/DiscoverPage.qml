import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Descubrir": featured app + rows of trending / popular / new / recently updated apps (Flathub).
Flickable {
    id: root
    property real bottomPad: 20
    signal openApp(string id)
    signal installApp(string id, string name)

    readonly property int side: 22

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
            onOpenApp: root.openApp(Discover.hero.id)
            onInstallApp: root.installApp(Discover.hero.id, Discover.hero.name)
        }

        Repeater {
            model: Discover.sections

            delegate: ColumnLayout {
                id: sec
                required property var modelData
                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    spacing: 0
                    StyledText {
                        text: root.sectionTitle(sec.modelData.id)
                        font.pixelSize: 19
                    }
                    StyledText {
                        text: root.sectionSubtitle(sec.modelData.id)
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 12
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 204
                    orientation: ListView.Horizontal
                    spacing: 12
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: sec.modelData.items
                    // Room for the card shadow/scale animation without clipping it
                    topMargin: 2
                    bottomMargin: 2

                    delegate: AppCard {
                        required property var modelData
                        app: modelData
                        footer: root.footerFor(sec.modelData.id)
                        onClicked: root.openApp(modelData.id)
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
