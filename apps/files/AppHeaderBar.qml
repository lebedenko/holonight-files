pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root
    objectName: "appHeaderBar"

    required property DirectoryController controller
    required property real sidebarWidth

    readonly property real breadcrumbLeftInset: root.sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal) + HnMetrics.horizontalPadding(HnControlSize.Normal)
    readonly property real breadcrumbPadding: HnMetrics.horizontalPadding(HnControlSize.Compact)

    horizontalPadding: 0
    dividerVisible: true
    dividerColor: HoloniightPalette.borderPassive

    Rectangle {
        anchors.fill: parent
        color: HoloniightPalette.surface
        z: -1
    }

    // The label is wrapped in a plain Item (rather than being the Component's root) because a
    // Loader binds an unsized root item's width/height to its own — a bare Label root would be
    // stretched to the full header height and render top-aligned instead of centered.
    content: Item {
        Rectangle {
            id: breadcrumbContainer
            objectName: "breadcrumbContainer"
            x: root.breadcrumbLeftInset - root.breadcrumbPadding
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(breadcrumbLabel.implicitWidth + 2 * root.breadcrumbPadding, parent.width - x - HnMetrics.internalSpacing(HnControlSize.Normal)))
            height: HnMetrics.controlHeight(HnControlSize.Compact)
            radius: height / 2
            color: HoloniightPalette.surfaceRaised

            HnLabel {
                id: breadcrumbLabel
                objectName: "breadcrumbLabel"
                anchors.fill: parent
                anchors.margins: root.breadcrumbPadding
                verticalAlignment: Text.AlignVCenter
                role: HnTypographyRole.Body
                elide: Text.ElideMiddle
                textFormat: Text.PlainText
                rawText: root.controller.currentPath
            }
        }
    }
}
