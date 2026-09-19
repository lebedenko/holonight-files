pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property DirectoryController controller
    readonly property real inset: HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property bool activationEnabled: root.controller.vim.currentMode === VimModeController.Normal && !root.controller.tasks.hasPrompt && !root.controller.quickLookOpen

    HnLabel {
        id: heading
        objectName: "placesHeading"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: root.inset
        anchors.leftMargin: root.inset + HnMetrics.horizontalPadding(HnControlSize.Compact)
        rawText: qsTr("Places")
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
    }

    ListView {
        id: list
        objectName: "placesListView"
        anchors.top: heading.bottom
        anchors.topMargin: root.inset
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.inset
        clip: true
        enabled: root.activationEnabled
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: root.controller.places
        spacing: HnMetrics.internalSpacing(HnControlSize.Compact)
        Controls.ScrollBar.vertical: Controls.ScrollBar {}

        function activate(): void {
            const place = currentItem as PlaceRow;
            if (root.activationEnabled && place)
                place.activate();
        }
        Keys.onReturnPressed: activate()
        Keys.onEnterPressed: activate()
        Keys.onSpacePressed: activate()

        // The wrapper keeps keyboard focus separate from exact-path selection in
        // HnSelectableDelegate (which otherwise also selects ListView.currentItem).
        delegate: PlaceRow {}
    }

    component PlaceRow: Item {
        id: row
        required property int index
        required property string name
        required property string path
        required property string iconName
        required property int origin
        required property int status
        required property bool startsBookmarks
        width: list.width
        // One extra compact-spacing token before the first bookmark row (REQ-F-028), on top of
        // ListView's own inter-row spacing; the visible delegate stays anchored to the bottom so
        // the gap appears above it, not inside it.
        readonly property real extraGap: row.startsBookmarks ? HnMetrics.internalSpacing(HnControlSize.Compact) : 0
        height: button.implicitHeight + row.extraGap

        function activate(): void {
            if (!root.activationEnabled)
                return;
            if (row.origin === PlacesModel.Bookmark)
                root.controller.activateBookmark(row.index);
            else
                root.controller.open(row.path);
        }

        HnListDelegate {
            id: button
            objectName: "placeDelegate"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            opacity: row.status === PlacesModel.Unavailable ? 0.5 : 1.0
            title: row.name
            sizeRole: HnControlSize.Compact
            focusPolicy: Qt.NoFocus
            highlighted: root.controller.currentPath === row.path
            leadingContentAlignment: Qt.AlignVCenter
            onClicked: row.activate()
            Accessible.description: row.status === PlacesModel.Unavailable ? qsTr("unavailable") : ""
            leadingContent: Item {
                implicitWidth: HnMetrics.iconSize(HnControlSize.Compact)
                implicitHeight: implicitWidth
                HnIcon {
                    id: themeIcon
                    objectName: "placeThemeIcon"
                    anchors.centerIn: parent
                    size: parent.implicitWidth
                    source: "image://icon/" + row.iconName
                    visible: !hasError
                }
                HnIcon {
                    objectName: "placeFallbackIcon"
                    anchors.centerIn: parent
                    size: parent.implicitWidth
                    source: themeIcon.hasError ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : ""
                    visible: themeIcon.hasError
                }
            }
            trailingContent: row.status === PlacesModel.Unavailable ? warningBadge : null
        }
        Component {
            id: warningBadge
            Item {
                implicitWidth: HnMetrics.iconSize(HnControlSize.Compact)
                implicitHeight: implicitWidth
                HnIcon {
                    id: warningThemeIcon
                    objectName: "placeWarningThemeIcon"
                    anchors.centerIn: parent
                    size: parent.implicitWidth
                    source: "image://icon/dialog-warning"
                    visible: !hasError
                }
                HnIcon {
                    objectName: "placeWarningFallbackIcon"
                    anchors.centerIn: parent
                    size: parent.implicitWidth
                    source: warningThemeIcon.hasError ? "qrc:/qt/qml/HolonightFiles/icons/warning-fallback.svg" : ""
                    visible: warningThemeIcon.hasError
                }
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: button.height
            color: "transparent"
            border.width: HnMetrics.focusBorderWidth
            border.color: HoloniightPalette.borderFocus
            visible: list.activeFocus && row.ListView.isCurrentItem
            Accessible.ignored: true
        }
    }
}
