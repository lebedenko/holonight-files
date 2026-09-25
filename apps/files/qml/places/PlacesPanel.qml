pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

// Content-sized: SidebarPanel's Flickable scrolls Places and Devices together (device-actions
// REQ-F-030, REQ-F-033), and its SidebarNavigator owns the keyboard cursor (REQ-F-049).
Item {
    id: root

    required property DirectoryController controller
    // True while the sidebar holds keyboard focus, which is when the cursor row is outlined.
    property bool keyboardFocus: false
    readonly property real inset: HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property bool activationEnabled: root.controller.sidebarActivationEnabled
    readonly property SidebarNavigator navigator: root.controller.sidebarNavigator
    readonly property alias list: list

    implicitHeight: list.y + list.height + root.inset

    function activateRow(index: int): void {
        const place = list.itemAtIndex(index) as PlaceRow;
        if (place)
            place.activate();
    }

    HnLabel {
        id: heading
        objectName: "placesHeading"
        x: root.inset + HnMetrics.horizontalPadding(HnControlSize.Compact)
        y: root.inset
        rawText: qsTr("Places")
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
    }

    ListView {
        id: list
        objectName: "placesListView"
        x: root.inset
        y: heading.y + heading.height + root.inset
        width: root.width - 2 * root.inset
        height: contentHeight
        interactive: false
        enabled: root.activationEnabled
        keyNavigationEnabled: false
        currentIndex: root.navigator.section === SidebarNavigator.Places ? root.navigator.index : -1
        model: root.controller.places
        spacing: HnMetrics.internalSpacing(HnControlSize.Compact)

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

        readonly property bool isCursor: root.navigator.section === SidebarNavigator.Places && root.navigator.index === row.index

        function activate(): void {
            if (!root.activationEnabled)
                return;
            root.navigator.setCursor(SidebarNavigator.Places, row.index);
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
                    rendering: HnIcon.Original
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
                    rendering: HnIcon.Original
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
            visible: root.keyboardFocus && row.isCursor
            Accessible.ignored: true
        }
    }
}
