import QtQuick
import QtQuick.Shapes

// Feedback of a control under the pointer: a faint wash while it rests on it, and a ripple that spreads from the
// point of the press. It fills its parent and follows the rounding of the parent's shape.
MouseArea {
    id: root

    property bool disabled: false
    // The control changes shape while it is pressed: the ripple travels a little further to still cover it
    property bool shapeMorph: false
    property color color: Theme.onSurface
    property real radius: parent && parent.radius !== undefined ? parent.radius : 0
    // How visible the wash is; a control that paints its own can set it to 0
    property real stateOpacity: containsMouse && !disabled ? 0.08 : 0
    property real pressX: width / 2
    property real pressY: height / 2

    // Progress of the ripple (0 = just born, 1 = it has reached the farthest corner) and how visible it is
    property real wave: 0
    property real tint: 0

    readonly property real reach: 1.25 * (Math.hypot(Math.max(pressX, width - pressX), Math.max(pressY, height - pressY)) + (shapeMorph ? 24 : 0))
    readonly property real corner: Math.max(0, Math.min(radius, width / 2, height / 2))
    // The ripple only starts to fade once it has spread everywhere and the button is let go
    readonly property bool settled: wave >= 0.99 && !pressed

    function startRipple(x: real, y: real): void {
        pressX = x;
        pressY = y;
        fade.stop();
        wave = 0;
        tint = 0.12;
        spread.restart();
    }

    anchors.fill: parent
    enabled: !disabled
    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    cursorShape: disabled ? Qt.ArrowCursor : Qt.PointingHandCursor

    onPressed: event => startRipple(event.x, event.y)
    onSettledChanged: {
        if (settled && tint > 0)
            fade.restart();
    }

    NumberAnimation {
        id: spread

        target: root
        property: "wave"
        from: 0
        to: 1
        duration: 500
        easing.type: Easing.OutCubic
        // A quick tap still shows the whole ripple
        alwaysRunToEnd: true
    }

    NumberAnimation {
        id: fade

        target: root
        property: "tint"
        to: 0
        duration: 260
        easing.type: Easing.OutCubic
    }

    Rectangle {
        anchors.fill: parent
        radius: root.corner
        color: root.color
        opacity: root.stateOpacity

        Behavior on opacity {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.tint > 0
        opacity: root.tint
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: -1

            // Solid at the centre with a soft rim that grows with the ripple and hardens as it ends, so the
            // whole control ends up tinted evenly before it fades
            fillGradient: RadialGradient {
                centerX: root.pressX
                centerY: root.pressY
                focalX: centerX
                focalY: centerY
                centerRadius: Math.max(0.5, root.wave * root.reach)

                GradientStop {
                    position: 0
                    color: root.color
                }

                GradientStop {
                    position: 0.55
                    color: root.color
                }

                GradientStop {
                    position: 1
                    color: Qt.alpha(root.color, Math.pow(root.wave, 4))
                }
            }

            PathRectangle {
                width: root.width
                height: root.height
                radius: root.corner
            }
        }
    }
}
