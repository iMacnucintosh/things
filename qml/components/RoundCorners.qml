import QtQuick
import QtQuick.Shapes

// Rounds the corners of whatever sits underneath without shader effects: four quarter-circle
// cut-outs painted in the surrounding colour. Put it on top of an image (anchors.fill: image).
Item {
    id: root
    property int radius: 16
    property color colour: Theme.surface

    component Corner: Shape {
        width: root.radius
        height: root.radius
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.colour
            strokeWidth: -1
            startX: 0; startY: 0
            PathLine { x: root.radius; y: 0 }
            PathArc { x: 0; y: root.radius; radiusX: root.radius; radiusY: root.radius; direction: PathArc.Counterclockwise }
            PathLine { x: 0; y: 0 }
        }
    }
    Corner { anchors.left: parent.left; anchors.top: parent.top }
    Corner { anchors.right: parent.right; anchors.top: parent.top; rotation: 90 }
    Corner { anchors.right: parent.right; anchors.bottom: parent.bottom; rotation: 180 }
    Corner { anchors.left: parent.left; anchors.bottom: parent.bottom; rotation: 270 }
}
