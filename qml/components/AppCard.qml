import QtQuick
import QtQuick.Layouts

// App-store card: icon, name, two-line summary and a footer (installs / how new it is).
Item {
    id: root
    property var app: ({})
    property string source: "flatpak"      // flatpak | pacman
    // "installs" | "added" | "updated" | "popularity": what the footer shows
    property string footer: "installs"
    signal clicked()

    // Re-evaluates when the installed list refreshes.
    readonly property bool installed: { Things.installedTotal; return Things.isInstalled(source, app.id) }

    implicitWidth: 176
    implicitHeight: 200

    function ago(ts) {
        const days = Math.max(0, Math.floor((Date.now() / 1000 - ts) / 86400))
        if (days < 1) return qsTr("today")
        if (days === 1) return qsTr("yesterday")
        if (days < 14) return qsTr("%1 days ago").arg(days)
        if (days < 60) return qsTr("%1 weeks ago").arg(Math.round(days / 7))
        return qsTr("%1 months ago").arg(Math.round(days / 30))
    }
    readonly property string footerText: footer === "added" ? qsTr("New · %1").arg(ago(app.added))
                                       : footer === "updated" ? qsTr("Updated %1").arg(ago(app.updated))
                                       : footer === "popularity" ? qsTr("%1% of users").arg(Math.round(app.popularity))
                                       : qsTr("%1 installs").arg(Discover.formatInstalls(app.installs))
    readonly property string footerIcon: footer === "added" ? "fiber_new" : footer === "updated" ? "update"
                                       : footer === "popularity" ? "group" : "download"

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 20
        color: area.containsMouse ? Theme.surfaceContainerHigh : Theme.surfaceContainer
        border.width: 1
        border.color: Qt.alpha(Theme.outlineVariant, 0.5)
        scale: area.pressed ? 0.97 : 1
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            Item {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64

                Image {
                    id: icon
                    anchors.fill: parent
                    source: root.app.icon || ""
                    sourceSize: Qt.size(128, 128)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
                // Until the icon arrives (or if it never does): initial on a tonal circle.
                Rectangle {
                    anchors.fill: parent
                    radius: 18
                    color: Theme.secondaryContainer
                    visible: icon.status !== Image.Ready
                    StyledText {
                        anchors.centerIn: parent
                        text: (root.app.name || "?").charAt(0).toUpperCase()
                        color: Theme.onSecondaryContainer
                        font.pixelSize: 26
                    }
                }
            }

            Item { Layout.fillWidth: true }

            MaterialIcon {
                visible: root.installed
                icon: "check_circle"
                fill: 1
                size: 20
                colour: Theme.primary
                Layout.alignment: Qt.AlignTop
            }
        }

        StyledText {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: root.app.name || ""
            font.pixelSize: 15
            elide: Text.ElideRight
        }
        StyledText {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: root.app.summary || ""
            color: Theme.onSurfaceVariant
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            MaterialIcon { icon: root.footerIcon; size: 14; colour: Theme.onSurfaceVariant }
            StyledText {
                Layout.fillWidth: true
                text: root.footerText
                color: Theme.onSurfaceVariant
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
