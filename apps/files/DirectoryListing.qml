pragma ComponentBehavior: Bound

import "InspectionKeys.js" as InspectionKeys
import QtQuick
import Holonight.Controls

Item {
    id: root

    required property DirectoryController controller
    property bool previousQuickLookOpen: false

    function formatSize(bytes: real): string {
        if (bytes < 0)
            return "";
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes);
        const units = ["KB", "MB", "GB", "TB"];
        let value = bytes / 1024;
        let unitIndex = 0;
        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024;
            unitIndex += 1;
        }
        return qsTr("%1 %2").arg(value.toFixed(1)).arg(units[unitIndex]);
    }

    ListView {
        id: listView
        objectName: "directoryListView"

        anchors.fill: parent
        clip: true
        focus: true
        visible: !root.controller || root.controller.directoryError.length === 0
        model: root.controller ? root.controller.listing : null
        currentIndex: root.controller ? root.controller.cursorRow : -1
        onCurrentIndexChanged: {
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain);
        }

        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, false)
        Keys.onPressed: event => InspectionKeys.press(event, root.controller, false)
        Keys.onReleased: event => InspectionKeys.release(event)
        Connections {
            target: root.controller
            function onChanged(): void {
                if (root.previousQuickLookOpen && !root.controller.quickLookOpen)
                    listView.forceActiveFocus();
                root.previousQuickLookOpen = root.controller.quickLookOpen;
            }
        }
        Component.onCompleted: forceActiveFocus()

        delegate: HnListDelegate {
            id: delegate

            required property int index
            required property string name
            required property bool isDir
            required property real size
            required property var modified
            required property bool statFailed
            required property string statError

            objectName: "directoryEntryDelegate"
            width: listView.width
            highlighted: ListView.isCurrentItem
            title: name
            subtitle: statFailed ? statError : (isDir ? qsTr("Folder") : Qt.formatDateTime(modified, "yyyy-MM-dd HH:mm"))
            metadata: isDir ? "" : root.formatSize(size)
            trailingContent: statFailed ? errorIndicator : null

            Component {
                id: errorIndicator

                HnStatusIndicator {
                    status: HnStatusIndicator.Warning
                }
            }

            Keys.priority: Keys.BeforeItem
            Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, false)
            Keys.onPressed: event => InspectionKeys.press(event, root.controller, false)
            Keys.onReleased: event => InspectionKeys.release(event)

            onClicked: root.controller.openEntry(delegate.index)
        }
    }

    HnEmptyState {
        objectName: "directoryErrorState"
        anchors.centerIn: parent
        visible: root.controller && root.controller.directoryError.length > 0
        titleText: qsTr("Can't open this folder")
        descriptionText: root.controller ? root.controller.directoryError : ""
    }
}
