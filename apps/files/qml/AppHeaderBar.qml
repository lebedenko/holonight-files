pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root
    objectName: "appHeaderBar"

    required property DirectoryController controller
    required property real sidebarWidth

    // Breadcrumb text starts at the listing's left edge (Main.qml content row: sidebar + spacing), which
    // is also the line-number gutter's left edge (line-number-gutter REQ-F-015).
    readonly property real breadcrumbLeftInset: root.sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property real breadcrumbPadding: HnMetrics.horizontalPadding(HnControlSize.Compact)
    // Same gating as the Ctrl+O/Ctrl+I shortcuts (REQ-F-032).
    readonly property bool historyEnabled: root.controller.vim.currentMode === VimModeController.Normal && !root.controller.tasks.hasPrompt

    horizontalPadding: 0
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
        // History buttons sit inside the sidebar-width region, leaving the breadcrumb's x untouched
        // (navigation-history REQ-F-030..036). NoFocus keeps Vim keys on the listing after a click.
        HnIconButton {
            id: backButton
            objectName: "historyBackButton"
            x: HnMetrics.internalSpacing(HnControlSize.Normal)
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.NoFocus
            icon.source: "qrc:/qt/qml/HolonightFiles/icons/go-back.svg"
            enabled: root.historyEnabled && root.controller.canGoBack
            Accessible.name: qsTr("Back")
            onClicked: root.controller.goBack()
        }

        HnIconButton {
            id: forwardButton
            objectName: "historyForwardButton"
            x: backButton.x + backButton.width
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.NoFocus
            icon.source: "qrc:/qt/qml/HolonightFiles/icons/go-forward.svg"
            enabled: root.historyEnabled && root.controller.canGoForward
            Accessible.name: qsTr("Forward")
            onClicked: root.controller.goForward()
        }

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
