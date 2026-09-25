import QtQuick
import Holonight.Core

// Used-space bar composed from primitives: Holonight.Controls' ProgressBar fixes its fill to the
// primary role, and the threshold colours below need a per-instance fill (device-actions REQ-C-005).
Rectangle {
    id: root

    // Used space over total space, 0..1 (REQ-F-046).
    property real fraction: 0
    // Each threshold applies from its percentage inclusive (REQ-F-047, REQ-F-048).
    readonly property color fillColor: root.fraction >= 0.95 ? HoloniightPalette.warning : root.fraction >= 0.90 ? HoloniightPalette.accentViolet : HoloniightPalette.primary

    implicitHeight: 4
    radius: height / 2
    color: HoloniightPalette.surfaceRaised
    Accessible.role: Accessible.ProgressBar

    Rectangle {
        objectName: "capacityFill"
        width: Math.round(parent.width * Math.max(0, Math.min(1, root.fraction)))
        height: parent.height
        radius: parent.radius
        color: root.fillColor
    }
}
