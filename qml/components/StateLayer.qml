import QtQuick
import QtQuick.Shapes

// Port of Sylphrena StateLayer: hover wash + Material ripple on press/release.
MouseArea {
    id: root

    property bool disabled: false
    property bool shapeMorph: false
    property real stateOpacity: (containsMouse && !disabled) ? 0.08 : 0
    property real pressX: width / 2
    property real pressY: height / 2
    property real circleRadius: 0
    property real endRadiusAtPress: 0
    property color color: Theme.onSurface
    property real radius: parent && parent.radius !== undefined ? parent.radius : 0

    readonly property real endRadius: {
        const d1 = distSq(0, 0)
        const d2 = distSq(width, 0)
        const d3 = distSq(0, height)
        const d4 = distSq(width, height)
        return (Math.sqrt(Math.max(d1, d2, d3, d4)) + (shapeMorph ? 24 : 0)) * 1.3
    }

    function distSq(x, y) {
        return (pressX - x) * (pressX - x) + (pressY - y) * (pressY - y)
    }

    function clampR(r) {
        return Math.max(0, Math.min(r, width / 2, height / 2))
    }

    function clamp01(v) {
        return Math.max(0, Math.min(1, v))
    }

    function pressAt(x, y) {
        pressX = x
        pressY = y
        fadeAnim.stop()
        circleRadius = 0
        circle.opacity = 0.12
        endRadiusAtPress = endRadius
        rippleAnim.restart()
    }

    anchors.fill: parent
    enabled: !disabled
    hoverEnabled: true
    cursorShape: disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
    acceptedButtons: Qt.LeftButton

    onPressed: (e) => pressAt(e.x, e.y)

    onPressedChanged: {
        if (!pressed && !rippleAnim.running && circle.opacity > 0)
            fadeAnim.start()
    }

    onCircleRadiusChanged: {
        if (!pressed && circleRadius > endRadiusAtPress * 0.99 && !fadeAnim.running)
            fadeAnim.start()
    }

    NumberAnimation {
        id: rippleAnim
        target: root
        property: "circleRadius"
        to: root.endRadius
        duration: 520
        easing.type: Easing.OutCubic
        alwaysRunToEnd: true
    }

    NumberAnimation {
        id: fadeAnim
        target: circle
        property: "opacity"
        to: 0
        duration: 280
        easing.type: Easing.OutCubic
    }

    Rectangle {
        id: base
        anchors.fill: parent
        radius: root.radius
        color: root.color
        opacity: root.stateOpacity
        z: 0

        Behavior on opacity {
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
    }

    Shape {
        id: circle
        anchors.fill: parent
        opacity: 0
        z: 1
        preferredRendererType: Shape.CurveRenderer
        clip: false

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: root.color
            fillGradient: RadialGradient {
                centerX: root.pressX
                centerY: root.pressY
                centerRadius: Math.max(0.01, root.circleRadius)
                focalX: centerX
                focalY: centerY

                GradientStop {
                    position: 0
                    color: Qt.alpha(root.color, 1)
                }
                GradientStop {
                    position: {
                        const t = 1 - 0.2 * root.endRadius / Math.max(0.01, root.circleRadius)
                        return Math.max(0.01, Math.min(0.99, t))
                    }
                    color: Qt.alpha(root.color, 1)
                }
                GradientStop {
                    position: 1
                    color: Qt.alpha(root.color, root.clamp01((root.circleRadius / Math.max(0.01, root.endRadius) - 0.9) / 0.1))
                }
            }

            startX: root.clampR(root.radius)
            startY: 0

            PathLine {
                x: root.width - root.clampR(root.radius)
                y: 0
            }
            PathArc {
                relativeX: root.clampR(root.radius)
                relativeY: root.clampR(root.radius)
                radiusX: root.clampR(root.radius)
                radiusY: root.clampR(root.radius)
            }
            PathLine {
                x: root.width
                y: root.height - root.clampR(root.radius)
            }
            PathArc {
                relativeX: -root.clampR(root.radius)
                relativeY: root.clampR(root.radius)
                radiusX: root.clampR(root.radius)
                radiusY: root.clampR(root.radius)
            }
            PathLine {
                x: root.clampR(root.radius)
                y: root.height
            }
            PathArc {
                relativeX: -root.clampR(root.radius)
                relativeY: -root.clampR(root.radius)
                radiusX: root.clampR(root.radius)
                radiusY: root.clampR(root.radius)
            }
            PathLine {
                x: 0
                y: root.clampR(root.radius)
            }
            PathArc {
                x: root.clampR(root.radius)
                y: 0
                radiusX: root.clampR(root.radius)
                radiusY: root.clampR(root.radius)
            }
        }
    }
}
