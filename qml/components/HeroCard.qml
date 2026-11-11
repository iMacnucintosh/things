import QtQuick
import QtQuick.Layouts

// Featured app: large banner with a real screenshot on the right (icon when there is none yet).
Item {
    id: root
    property var app: ({})
    signal openApp()
    signal installApp()

    readonly property bool installed: { Things.installedTotal; return Things.isInstalled("flatpak", app.id) }
    readonly property bool hasShot: (app.screenshot || "").length > 0

    implicitHeight: 248

    Rectangle {
        anchors.fill: parent
        radius: 28
        color: Theme.primaryContainer
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 22

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                implicitWidth: tag.implicitWidth + 20
                implicitHeight: 24
                radius: 12
                color: Qt.alpha(Theme.onPrimaryContainer, 0.12)
                StyledText {
                    id: tag
                    anchors.centerIn: parent
                    text: qsTr("Featured today")
                    color: Theme.onPrimaryContainer
                    font.pixelSize: 11
                }
            }

            RowLayout {
                Layout.topMargin: 6
                spacing: 14
                Image {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    source: root.app.icon || ""
                    sourceSize: Qt.size(112, 112)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    StyledText {
                        Layout.fillWidth: true
                        text: root.app.name || ""
                        color: Theme.onPrimaryContainer
                        font.pixelSize: 26
                        elide: Text.ElideRight
                    }
                    StyledText {
                        Layout.fillWidth: true
                        visible: (root.app.developer || "").length > 0
                        text: root.app.developer || ""
                        color: Theme.onPrimaryContainer
                        opacity: 0.7
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }

            StyledText {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 4
                text: root.app.summary || ""
                color: Theme.onPrimaryContainer
                font.pixelSize: 14
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                verticalAlignment: Text.AlignTop
            }

            RowLayout {
                spacing: 10
                ActionButton {
                    text: root.installed ? qsTr("Installed") : qsTr("Install")
                    icon: root.installed ? "check" : "download"
                    tone: root.installed ? "tonal" : "primary"
                    enabled: !root.installed
                    onClicked: root.installApp()
                }
                ActionButton {
                    text: qsTr("See details")
                    tone: "text"
                    onClicked: root.openApp()
                }
                StyledText {
                    visible: (root.app.installs || 0) > 0
                    text: qsTr("%1 installs this month").arg(Discover.formatInstalls(root.app.installs))
                    color: Theme.onPrimaryContainer
                    opacity: 0.7
                    font.pixelSize: 12
                    Layout.leftMargin: 6
                }
            }
        }

        // Screenshot with rounded corners. No shader effects: the four outer corners are covered
        // with quarter-circle cut-outs in the banner colour (works with any Qt Quick renderer).
        Item {
            id: shotBox
            Layout.fillHeight: true
            Layout.preferredWidth: Math.round(root.width * 0.40)
            // Hidden when the banner is narrow (detail drawer open): a sliver of screenshot looks broken
            visible: root.hasShot && root.width >= 760
            readonly property int r: 18

            Image {
                id: shot
                anchors.fill: parent
                source: root.hasShot ? root.app.screenshot : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
            // Placeholder while the screenshot downloads
            Rectangle {
                anchors.fill: parent
                radius: shotBox.r
                color: Qt.alpha(Theme.onPrimaryContainer, 0.08)
                visible: shot.status !== Image.Ready
            }

            RoundCorners {
                anchors.fill: parent
                radius: shotBox.r
                colour: Theme.primaryContainer
                visible: shot.status === Image.Ready
            }
        }
    }
}
