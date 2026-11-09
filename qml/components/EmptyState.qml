import QtQuick
import QtQuick.Layouts

// Centered empty / loading / error placeholder.
Item {
    id: root
    property string icon: "inventory_2"
    property string title: ""
    property string subtitle: ""
    property bool busy: false
    property string tone: "normal"   // normal | error
    default property alias extra: extraCol.data

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 88
            Layout.preferredHeight: 88
            radius: 28
            color: root.tone === "error" ? Qt.alpha(Theme.error, 0.14) : Theme.surfaceContainerHigh

            MaterialIcon {
                anchors.centerIn: parent
                icon: root.busy ? "progress_activity" : root.icon
                size: 40
                fill: root.busy ? 0 : 1
                colour: root.tone === "error" ? Theme.error : Theme.onSurfaceVariant
                RotationAnimator on rotation {
                    running: root.busy
                    from: 0; to: 360; duration: 1000; loops: Animation.Infinite
                }
            }
        }
        StyledText {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: root.title
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        StyledText {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.subtitle
            color: Theme.onSurfaceVariant
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            id: extraCol
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
        }
    }
}
