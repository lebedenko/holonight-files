pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root
    objectName: "appHeaderBar"
    sizeRole: HnControlSize.Xs

    required property DirectoryController controller
    required property Item listingPanel
    property bool showCloseButton: true
    property bool editingLocation: false
    signal locationEditFinished
    function editLocation(): void {
        if (!root.historyEnabled)
            return;
        root.editingLocation = true;
        (root.contentItem as HeaderContent).editLocation();
    }
    signal closeRequested

    function focusCurrentSegment(): bool {
        const headerContent = root.contentItem as HeaderContent;
        return headerContent ? headerContent.focusCurrentSegment() : false;
    }

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
    content: HeaderContent {}

    component ViewButton: HnIconButton {
        id: button
        sizeRole: HnControlSize.Compact
        focusPolicy: Qt.TabFocus
        checkable: true
        autoExclusive: true
        enabled: root.historyEnabled
        icon.color: checked && enabled ? HoloniightPalette.primary : "transparent"

        Binding {
            target: button.background
            property: "color"
            when: button.checked && (!button.enabled || (!button.down && !(button.HnInputInteraction.hoverAllowed && button.hovered)))
            value: HoloniightPalette.surfaceRaised
        }
    }

    component HeaderContent: Item {
        function editLocation(): void {
            location.text = root.controller.currentPath;
            location.forceActiveFocus();
            location.selectAll();
        }
        function focusCurrentSegment(): bool {
            return breadcrumbLabel.focusCurrentSegment();
        }

        // History buttons sit inside the sidebar region, leaving the listing-aligned breadcrumb clear
        // (navigation-history REQ-F-030..036). TabFocus keeps pointer clicks from stealing listing focus.
        HnIconButton {
            id: backButton
            objectName: "historyBackButton"
            x: HnMetrics.internalSpacing(HnControlSize.Normal)
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.TabFocus
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
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.TabFocus
            icon.name: "go-next"
            enabled: root.historyEnabled && root.controller.canGoForward
            Accessible.name: qsTr("Forward")
            onClicked: root.controller.goForward()
        }

        HnIconButton {
            id: upButton
            objectName: "parentUpButton"
            x: forwardButton.x + forwardButton.width
            anchors.verticalCenter: parent.verticalCenter
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.TabFocus
            icon.name: "go-up"
            enabled: root.historyEnabled && root.controller.canGoUp
            Accessible.name: qsTr("Up")
            onClicked: root.controller.goUp()
        }
        Item {
            id: breadcrumbContainer
            objectName: "breadcrumbContainer"
            x: root.breadcrumbLeftInset
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(root.listingPanel.width, (root.showCloseButton ? closeButton.x - 8 : parent.width - 12) - x - viewSelector.width - 8))
            height: HnMetrics.controlHeight(HnControlSize.Normal)

            HnBreadcrumbBar {
                id: breadcrumbLabel
                objectName: "breadcrumbLabel"
                editable: true
                visible: !root.editingLocation
                onLocationEditRequested: root.editLocation()
                vimNavigation: HnInputPreferences.vimNavigation
                clip: true
                sizeRole: HnControlSize.Normal
                anchors.fill: parent
                enabled: root.historyEnabled && !root.controller.quickLookOpen
                segments: root.controller.breadcrumbLabels
                onSegmentActivated: index => root.controller.activateBreadcrumb(index)
            }
            Controls.TextField {
                id: location
                objectName: "locationEditor"
                anchors.fill: parent
                visible: root.editingLocation
                Accessible.name: qsTr("Location")
                onAccepted: {
                    const path = text.startsWith("~/") ? root.controller.finder.homePath + text.slice(1) : text;
                    root.controller.open(path);
                    root.editingLocation = false;
                    root.locationEditFinished();
                }
                onActiveFocusChanged: if (!activeFocus)
                    root.editingLocation = false
                Keys.onEscapePressed: {
                    root.editingLocation = false;
                    root.locationEditFinished();
                }
            }
        }
        Row {
            id: viewSelector
            objectName: "viewSelector"
            x: breadcrumbContainer.x + breadcrumbContainer.width + 8
            anchors.verticalCenter: parent.verticalCenter
            ViewButton {
                objectName: "listViewButton"
                icon.name: "view-list-details"
                checked: !root.controller.iconView
                Accessible.name: qsTr("List")
                onClicked: root.controller.setIconView(false)
            }
            ViewButton {
                objectName: "iconViewButton"
                icon.name: "view-grid"
                checked: root.controller.iconView
                Accessible.name: qsTr("Icons")
                onClicked: root.controller.setIconView(true)
            }
        }
        HnHeaderCloseButton {
            id: closeButton
            objectName: "closeButton"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            focusPolicy: Qt.TabFocus
            visible: root.showCloseButton
            enabled: visible
            Accessible.ignored: !visible
            onClicked: root.closeRequested()
        }
    }
}
