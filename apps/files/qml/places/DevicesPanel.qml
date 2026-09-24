pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root
    required property DirectoryController controller
    readonly property bool activationEnabled: controller.vim.currentMode === VimModeController.Normal && !controller.tasks.hasPrompt && !controller.quickLookOpen
    spacing: HnMetrics.internalSpacing(HnControlSize.Compact)
    HnLabel {
        rawText: qsTr("Devices")
        role: HnTypographyRole.Caption
        Layout.leftMargin: 12
    }
    HnLabel {
        Layout.fillWidth: true
        rawText: root.controller.devices.errorMessage
        visible: rawText.length > 0
        wrapMode: Text.Wrap
        color: HoloniightPalette.error
    }
    ListView {
        id: list
        objectName: "devicesListView"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 8
        model: root.controller.devices
        clip: true
        spacing: 6
        enabled: root.activationEnabled
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
        function activate(): void {
            const row = currentItem as DeviceRow;
            if (row && root.activationEnabled)
                root.controller.devices.activate(row.targetId);
        }
        Keys.onReturnPressed: activate()
        Keys.onEnterPressed: activate()
        Keys.onSpacePressed: activate()
        delegate: DeviceRow {}
    }
    HnLabel {
        Layout.fillWidth: true
        visible: root.controller.devices.confirmationText.length > 0
        rawText: root.controller.devices.confirmationText
        wrapMode: Text.Wrap
    }
    Flow {
        Layout.fillWidth: true
        visible: root.controller.devices.confirmationText.length > 0
        enabled: root.activationEnabled
        Controls.Button {
            text: qsTr("Power off")
            onClicked: root.controller.devices.confirmPowerOff()
        }
        Controls.Button {
            text: qsTr("Cancel")
            onClicked: root.controller.devices.cancelPowerOff()
        }
    }
    component DeviceRow: Item {
        id: row
        required property string targetId
        required property string driveId
        required property string name
        required property string stateText
        required property bool canActivate
        required property bool canUnmount
        required property bool canEject
        required property bool canPowerOff
        implicitWidth: list.width
        implicitHeight: content.implicitHeight
        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            HnListDelegate {
                Layout.fillWidth: true
                title: row.name
                sizeRole: HnControlSize.Compact
                enabled: row.canActivate
                onClicked: root.controller.devices.activate(row.targetId)
                Accessible.description: row.stateText
            }
            HnLabel {
                Layout.fillWidth: true
                rawText: row.stateText
                wrapMode: Text.Wrap
                role: HnTypographyRole.Caption
            }
            Flow {
                Layout.fillWidth: true
                Controls.Button {
                    text: qsTr("Unmount")
                    visible: row.canUnmount
                    onClicked: root.controller.devices.unmount(row.targetId)
                }
                Controls.Button {
                    text: qsTr("Eject")
                    visible: row.canEject
                    onClicked: root.controller.devices.eject(row.driveId)
                }
                Controls.Button {
                    text: qsTr("Power off…")
                    visible: row.canPowerOff
                    onClicked: root.controller.devices.requestPowerOff(row.driveId)
                }
            }
        }
    }
}
