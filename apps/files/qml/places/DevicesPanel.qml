pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Content-sized Devices section of SidebarPanel. Each row offers at most one removal control, whose
// verb the model has already chosen (device-actions REQ-F-008..010); activation mounts (REQ-F-012).
Item {
    id: root
    required property DirectoryController controller
    // True while the sidebar holds keyboard focus, which is when the cursor row is outlined.
    property bool keyboardFocus: false
    readonly property real inset: HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property bool activationEnabled: root.controller.sidebarActivationEnabled
    readonly property SidebarNavigator navigator: root.controller.sidebarNavigator
    // Operation errors go to the window status bar, not here (REQ-F-029).
    readonly property bool hasContent: root.controller.devices.count > 0
    readonly property alias list: list

    implicitHeight: list.y + list.height + root.inset

    function activateRow(index: int): void {
        const row = list.itemAtIndex(index) as DeviceRow;
        if (row && root.activationEnabled)
            root.controller.devices.activate(row.targetId);
    }
    function removeRow(index: int): void {
        const row = list.itemAtIndex(index) as DeviceRow;
        if (row && root.activationEnabled)
            root.controller.devices.remove(row.targetId);
    }

    HnLabel {
        id: heading
        objectName: "devicesHeading"
        x: root.inset + HnMetrics.horizontalPadding(HnControlSize.Compact)
        y: root.inset
        rawText: qsTr("Devices")
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
    }
    ListView {
        id: list
        objectName: "devicesListView"
        x: root.inset
        y: heading.y + heading.height + root.inset
        width: root.width - 2 * root.inset
        height: contentHeight
        interactive: false
        keyNavigationEnabled: false
        currentIndex: root.navigator.section === SidebarNavigator.Devices ? root.navigator.index : -1
        model: root.controller.devices
        spacing: HnMetrics.internalSpacing(HnControlSize.Compact)
        delegate: DeviceRow {}
    }

    component DeviceRow: Item {
        id: row
        required property int index
        required property string targetId
        required property string name
        required property string stateText
        required property string iconName
        required property int removalVerb
        required property string removalLabel
        required property string groupLabel
        required property bool groupStart
        required property bool busy
        required property bool canActivate
        required property bool capacityValid
        required property real capacityFraction
        required property string capacityText
        readonly property bool isCursor: root.navigator.section === SidebarNavigator.Devices && root.navigator.index === row.index
        readonly property real padding: HnMetrics.horizontalPadding(HnControlSize.Compact)

        width: list.width
        implicitHeight: body.y + body.height

        // Drive name above a drive's first row when it contributes several (REQ-F-018); absent,
        // and taking no space, otherwise (REQ-F-019).
        HnLabel {
            id: groupHeader
            objectName: "deviceGroupLabel"
            x: row.padding
            visible: row.groupStart
            height: visible ? implicitHeight : 0
            rawText: row.groupLabel
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
        }

        Item {
            id: body
            objectName: "deviceRowBody"
            y: groupHeader.height > 0 ? groupHeader.height + list.spacing : 0
            width: row.width
            height: Math.max(columns.implicitHeight, HnMetrics.controlHeight(HnControlSize.Compact)) + HnMetrics.internalSpacing(HnControlSize.Compact)
            Accessible.role: Accessible.Button
            Accessible.name: row.name
            Accessible.description: row.capacityValid ? row.capacityText : row.stateText

            Rectangle {
                anchors.fill: parent
                radius: HnAppearance.roundedRadius(HnSurfaceRole.Control, width, height, HnAppearance.revision)
                color: activation.containsMouse && activation.enabled ? HoloniightPalette.surfaceHover : "transparent"
                border.width: root.keyboardFocus && row.isCursor ? HnMetrics.focusBorderWidth : 0
                border.color: HoloniightPalette.borderFocus
            }
            // Below the columns, so a click on the removal control never also activates the row.
            MouseArea {
                id: activation
                objectName: "deviceActivationArea"
                anchors.fill: parent
                hoverEnabled: true
                enabled: row.canActivate && root.activationEnabled
                onClicked: {
                    root.navigator.setCursor(SidebarNavigator.Devices, row.index);
                    root.controller.devices.activate(row.targetId);
                }
            }

            // Three columns separated by spacing alone (REQ-F-034).
            RowLayout {
                id: columns
                objectName: "deviceColumns"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: row.padding
                anchors.rightMargin: HnMetrics.internalSpacing(HnControlSize.Compact)
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                // Column 1: the device icon, centred on the whole row (REQ-F-035..037).
                Item {
                    objectName: "deviceIcon"
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: HnMetrics.iconSize(HnControlSize.Compact)
                    implicitHeight: implicitWidth
                    HnIcon {
                        id: themeIcon
                        objectName: "deviceThemeIcon"
                        anchors.centerIn: parent
                        size: parent.implicitWidth
                        source: "image://icon/" + row.iconName
                        visible: !hasError
                    }
                    HnIcon {
                        objectName: "deviceFallbackIcon"
                        anchors.centerIn: parent
                        size: parent.implicitWidth
                        source: themeIcon.hasError ? "qrc:/qt/qml/HolonightFiles/icons/drive-fallback.svg" : ""
                        visible: themeIcon.hasError
                    }
                }

                // Column 2: name, capacity bar, capacity text (REQ-F-038).
                ColumnLayout {
                    objectName: "deviceContent"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: HnMetrics.internalSpacing(HnControlSize.Compact) / 2
                    HnLabel {
                        objectName: "deviceName"
                        Layout.fillWidth: true
                        rawText: row.name
                        elide: Text.ElideRight
                        opacity: row.canActivate || row.capacityValid ? 1 : 0.7
                    }
                    // Every row keeps the bar's slot, so mounted and unmounted rows are equally tall
                    // (REQ-F-043); without figures the caption shows the row's state instead.
                    CapacityBar {
                        objectName: "capacityBar"
                        Layout.fillWidth: true
                        opacity: row.capacityValid ? 1 : 0
                        fraction: row.capacityFraction
                    }
                    HnLabel {
                        objectName: "capacityText"
                        Layout.fillWidth: true
                        rawText: row.capacityValid ? row.capacityText : row.stateText
                        elide: Text.ElideRight
                        role: HnTypographyRole.Caption
                        color: HoloniightPalette.textMuted
                    }
                }

                // Column 3: the single removal control; one eject glyph for every verb, named by its
                // accessible label (REQ-F-014, REQ-F-040, REQ-NF-002).
                HnIconButton {
                    objectName: "deviceRemoveButton"
                    Layout.alignment: Qt.AlignVCenter
                    sizeRole: HnControlSize.Compact
                    visible: row.removalVerb !== DevicesModel.NoVerb
                    enabled: !row.busy && root.activationEnabled
                    focusPolicy: Qt.NoFocus
                    icon.source: "qrc:/qt/qml/HolonightFiles/icons/eject-fallback.svg"
                    Accessible.name: row.removalLabel
                    onClicked: {
                        root.navigator.setCursor(SidebarNavigator.Devices, row.index);
                        root.controller.devices.remove(row.targetId);
                    }
                }
            }
        }
    }
}
