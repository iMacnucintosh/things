import QtQuick
import QtQuick.Layouts

// One unified-list row: [app icon | origin tile] name · version / description   [repo chip] [action]
Item {
    id: root

    required property int index
    required property string pkgId
    required property string name
    required property string version
    required property string description
    required property string source
    required property string repo
    required property bool installed
    required property int votes
    required property bool outOfDate
    required property bool orphan
    property bool selected: false
    property bool current: false
    property bool busy: false
    property bool compact: false
    signal activated()
    signal actionRequested()

    readonly property string iconName: Things.iconFor(source, pkgId)

    width: ListView.view ? ListView.view.width : 600
    height: 68

    Rectangle {
        id: bg
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: 18
        color: root.selected ? Qt.alpha(Theme.primaryContainer, 0.34)
             : (layer.containsMouse ? Theme.surfaceContainerLow : "transparent")
        border.width: root.current ? 2 : 0
        border.color: Theme.primary
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    StateLayer {
        id: layer
        anchors.fill: bg
        radius: bg.radius
        onClicked: root.activated()
    }

    RowLayout {
        anchors.fill: bg
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14

        // Real app icon when the icon theme has one; origin glyph otherwise.
        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignVCenter
            radius: 14
            color: Theme.surfaceContainerHigh

            Image {
                anchors.centerIn: parent
                width: 32
                height: 32
                visible: root.iconName.length > 0
                source: root.iconName.length ? "image://theme/" + root.iconName : ""
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }
            SourceIcon {
                anchors.centerIn: parent
                visible: root.iconName.length === 0
                source: root.source
                size: 22
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
                    id: nameLabel
                    text: root.name
                    font.pixelSize: 15
                    elide: Text.ElideRight
                    Layout.maximumWidth: Math.max(80, root.width * 0.5)
                }
                StyledText {
                    Layout.fillWidth: true
                    text: root.version
                    color: Theme.onSurfaceVariant
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    opacity: 0.85
                }
            }
            StyledText {
                Layout.fillWidth: true
                text: root.description.length ? root.description : qsTr("Sin descripción")
                color: Theme.onSurfaceVariant
                font.pixelSize: 13
                elide: Text.ElideRight
                maximumLineCount: 1
                opacity: root.description.length ? 1 : 0.6
            }
        }

        // Origin + repo chip. AUR shows the vote count instead of a repo.
        Rectangle {
            visible: !root.compact
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28
            Layout.preferredWidth: chipRow.implicitWidth + 20
            radius: 14
            color: Theme.surfaceContainer
            border.width: 1
            border.color: Theme.outlineVariant

            RowLayout {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                SourceIcon {
                    source: root.source
                    size: 16
                    Layout.alignment: Qt.AlignVCenter
                }
                StyledText {
                    text: root.source === "aur"
                          ? (root.votes > 0 ? "AUR · " + (root.votes >= 1000 ? (root.votes / 1000).toFixed(1) + "k" : root.votes) + " ★" : "AUR")
                          : root.repo
                    color: root.outOfDate ? Theme.error : Theme.onSurfaceVariant
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        MaterialIcon {
            visible: root.installed
            Layout.alignment: Qt.AlignVCenter
            icon: "check_circle"
            size: 22
            fill: 1
            colour: Theme.primary
        }

        // Quick action: install or remove.
        Item {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                id: actBg
                anchors.fill: parent
                radius: actLayer.pressed ? 12 : 20
                color: root.installed ? Qt.alpha(Theme.error, 0.14) : Theme.secondaryContainer
                Behavior on radius { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
            MaterialIcon {
                anchors.centerIn: parent
                icon: root.busy ? "progress_activity" : (root.installed ? "delete" : "download")
                size: 20
                colour: root.installed ? Theme.error : Theme.onSecondaryContainer
                RotationAnimator on rotation {
                    running: root.busy
                    from: 0; to: 360; duration: 900; loops: Animation.Infinite
                }
            }
            StateLayer {
                id: actLayer
                radius: actBg.radius
                shapeMorph: true
                color: root.installed ? Theme.error : Theme.onSecondaryContainer
                disabled: root.busy
                onClicked: root.actionRequested()
            }
        }
    }
}
