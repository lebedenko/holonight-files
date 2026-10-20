pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root
    objectName: "appHeaderBar"
    sizeRole: HnControlSize.Xs

    required property DirectoryController controller
    required property Item listingPanel
    signal closeRequested

    function sceneX(item: Item): real {
        let result = 0;
        while (item) {
            result += item.x;
            item = item.parent;
        }
        return result;
    }
    readonly property real breadcrumbLeftInset: sceneX(listingPanel) - sceneX(root)
    readonly property bool historyEnabled: root.controller.historyNavigationEnabled

    horizontalPadding: 0
    verticalPadding: 5
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
        // History buttons sit inside the sidebar region, leaving the listing-aligned breadcrumb clear
        // (navigation-history REQ-F-030..036). NoFocus keeps Vim keys on the listing after a click.
        HnIconButton {
            id: backButton
            objectName: "historyBackButton"
            x: HnMetrics.internalSpacing(HnControlSize.Normal)
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Xs
            focusPolicy: Qt.NoFocus
            icon.name: "go-previous"
            enabled: root.historyEnabled && root.controller.canGoBack
            Accessible.name: qsTr("Back")
            onClicked: root.controller.goBack()
        }

        HnIconButton {
            id: forwardButton
            objectName: "historyForwardButton"
            x: backButton.x + backButton.width
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Xs
            focusPolicy: Qt.NoFocus
            icon.name: "go-next"
            enabled: root.historyEnabled && root.controller.canGoForward
            Accessible.name: qsTr("Forward")
            onClicked: root.controller.goForward()
        }

        HnHeaderCloseButton {
            objectName: "closeButton"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            focusPolicy: Qt.NoFocus
            onClicked: root.closeRequested()
        }

        Item {
            id: breadcrumbContainer
            objectName: "breadcrumbContainer"
            x: root.breadcrumbLeftInset
            anchors.verticalCenter: parent.verticalCenter
            width: root.listingPanel.width
            height: HnMetrics.controlHeight(HnControlSize.Normal)

            HnBreadcrumbBar {
                id: breadcrumbLabel
                objectName: "breadcrumbLabel"
                editable: false
                clip: true
                sizeRole: HnControlSize.Normal
                anchors.fill: parent
                enabled: root.historyEnabled && !root.controller.quickLookOpen
                segments: root.controller.breadcrumbLabels
                onSegmentActivated: index => root.controller.activateBreadcrumb(index)
            }
        }
    }
}
