import QtQuick

// Rotating progress glyph. Only exists while visible, so it never freezes at an odd angle.
MaterialIcon {
    id: root
    icon: "progress_activity"
    RotationAnimator on rotation {
        running: root.visible
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite
    }
}
