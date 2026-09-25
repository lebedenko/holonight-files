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
        const list = section === SidebarNavigator.Places ? places.list : devices.list;
        const item = list.itemAtIndex(index);
        if (!item)
            return;
        const top = item.mapToItem(column, 0, 0).y;
        if (top < root.contentY)
            root.contentY = top;
        else if (top + item.height > root.contentY + root.height)
            root.contentY = Math.min(top + item.height - root.height, Math.max(0, root.contentHeight - root.height));
    }

    Connections {
        target: root.navigator
        function onRevealRequested(section: int, index: int): void {
            root.reveal(section, index);
        }
    }

    Keys.onPressed: event => {
        if (!root.controller.sidebarActivationEnabled)
            return;
        if (event.key === Qt.Key_Down || event.text === "j") {
            event.accepted = root.navigator.moveDown();
        } else if (event.key === Qt.Key_Up || event.text === "k") {
            event.accepted = root.navigator.moveUp();
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            root.activateCursor();
            event.accepted = true;
        } else if (event.text === "x") {
            // Plain x, as in Vim; Shift+X stays unbound (REQ-F-021, REQ-F-024).
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
