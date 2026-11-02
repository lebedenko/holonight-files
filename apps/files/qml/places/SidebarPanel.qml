pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

// The sidebar's one scroll area: Places and bookmarks, a separator, then Devices, each exactly as
// tall as its rows (device-actions REQ-F-030..033). Keyboard movement, activation and removal all
// resolve through the controller's single SidebarNavigator cursor (REQ-F-049..054).
Flickable {
    id: root
    objectName: "sidebarPanel"

    required property DirectoryController controller
    readonly property SidebarNavigator navigator: root.controller.sidebarNavigator

    clip: true
    contentWidth: width
    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    activeFocusOnTab: true
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Sidebar")

    Controls.ScrollBar.vertical: Controls.ScrollBar {
        objectName: "sidebarScrollBar"
    }

    function activateCursor(): void {
        if (root.navigator.section === SidebarNavigator.Places)
            places.activateRow(root.navigator.index);
        else if (root.navigator.section === SidebarNavigator.Devices)
            devices.activateRow(root.navigator.index);
    }

    // Scrolls just far enough to show the cursor row, whose height varies with capacity and group
    // labels (REQ-F-053).
    function reveal(section: int, index: int): void {
        const tops = root.targetTops();
        const flat = section === SidebarNavigator.Places ? index : places.list.count + index;
        if (flat < 0 || flat >= tops.length)
            return;
        const top = tops[flat];
        const bottom = flat + 1 < tops.length ? tops[flat + 1] : root.contentHeight - places.inset;
        if (top < root.contentY)
            root.contentY = top;
        else if (bottom > root.contentY + root.height)
            root.contentY = Math.min(bottom - root.height, Math.max(0, root.contentHeight - root.height));
    }

    Connections {
        target: root.navigator
        function onRevealRequested(section: int, index: int): void {
            root.reveal(section, index);
        }
    }

    HnNavigation {
        id: navigation
        active: root.activeFocus && root.controller.sidebarActivationEnabled
        vimNavigation: HnInputPreferences.vimNavigation
    }

    // Measure typography/control metrics once, independently of virtualized rows.
    HnListDelegate {
        id: placeMetrics
        visible: false
        title: "M"
        sizeRole: HnControlSize.Xs
    }
    HnLabel {
        id: nameMetrics
        visible: false
        rawText: "M"
    }
    HnLabel {
        id: captionMetrics
        visible: false
        rawText: "M"
        role: HnTypographyRole.Caption
    }
    function targetTops(): list<real> {
        let tops = [];
        const gap = HnMetrics.internalSpacing(HnControlSize.Compact);
        let top = places.y + places.list.y;
        for (let i = 0; i < places.list.count; ++i) {
            tops.push(top);
            top += placeMetrics.implicitHeight + (root.navigator.startsBookmarks(i) ? gap : 0);
        }
        top = devices.y + devices.list.y;
        const bodyHeight = Math.max(nameMetrics.implicitHeight + captionMetrics.implicitHeight + 4 + gap, HnMetrics.controlHeight(HnControlSize.Compact)) + gap;
        for (let i = 0; i < devices.list.count; ++i) {
            tops.push(top);
            top += bodyHeight + (root.navigator.startsDeviceGroup(i) ? captionMetrics.implicitHeight + gap : 0) + gap;
        }
        return tops;
    }

    Keys.onPressed: event => {
        if (!root.controller.sidebarActivationEnabled || !root.activeFocus)
            return;
        const intent = navigation.interpret(event.key, event.text, event.modifiers, false, true);
        if (intent.consumed) {
            if (intent.command === HnNavigation.ToggleHidden)
                root.controller.toggleHidden();
            else if (intent.command === HnNavigation.Open)
                root.activateCursor();
            else
                root.navigator.navigate(intent.command, intent.count, root.height, targetTops());
            event.accepted = true;
        } else if (event.modifiers === Qt.NoModifier && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
            root.activateCursor();
            event.accepted = true;
        } else if (event.modifiers === Qt.NoModifier && event.text === "x") {
            if (root.navigator.section === SidebarNavigator.Devices)
                devices.removeRow(root.navigator.index);
            event.accepted = true;
        }
    }

    Column {
        id: column
        width: root.width
        // Each section carries its own inset top and bottom, so the separator sits midway between.
        spacing: 0

        PlacesPanel {
            id: places
            objectName: "placesPanel"
            width: column.width
            controller: root.controller
            keyboardFocus: root.activeFocus
        }
        HnSeparator {
            objectName: "sidebarSectionSeparator"
            width: column.width
            // Column skips invisible children, so an empty Devices section leaves no gap (REQ-F-032).
            visible: devices.visible
            color: HoloniightPalette.borderPassive
        }
        DevicesPanel {
            id: devices
            objectName: "devicesPanel"
            width: column.width
            visible: devices.hasContent
            controller: root.controller
            keyboardFocus: root.activeFocus
        }
    }
}
