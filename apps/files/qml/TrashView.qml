pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root
    objectName: "trashView"
    required property TaskManager tasks
    readonly property bool ready: !tasks.busy && !tasks.hasPrompt && !tasks.trashScanning
    property int restoreIndex: -1
    property url chosenFolder
    property string restoreReason

    function focusListing(): void {
        entries.forceActiveFocus(Qt.TabFocusReason);
    }

    function chooseDestination(index: int, name: string, reason: string): void {
        root.restoreIndex = index;
        root.restoreReason = reason;
        destinationName.text = name;
        root.chosenFolder = "";
        destination.open();
    }

    Connections {
        target: root.tasks
        function onTrashChanged(): void {
            entries.currentIndex = -1;
        }
        function onRestoreAlternativeRequested(name: string, reason: string): void {
            if (root.visible)
                root.chooseDestination(-1, name, reason);
        }
        function onChanged(): void {
            if (root.visible && root.tasks.promptKind === TaskManager.PermanentDeleteConfirm) {
                confirmation.requestId = root.tasks.promptId;
                confirmation.open();
            } else if (confirmation.visible) {
                confirmation.close();
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            HnLabel {
                Layout.fillWidth: true
                rawText: qsTr("Trash")
            }
            Controls.Button {
                text: qsTr("Refresh")
                enabled: root.ready
                onClicked: root.tasks.scanTrash()
            }
            Controls.Button {
                text: qsTr("Empty Trash")
                objectName: "emptyTrashButton"
                enabled: root.ready && entries.count > 0
                onClicked: root.tasks.emptyTrash()
            }
        }

        ListView {
            id: entries
            objectName: "trashEntries"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: HnMetrics.internalSpacing(HnControlSize.Normal)
            Layout.rightMargin: Layout.leftMargin
            clip: true
            focus: root.visible
            activeFocusOnTab: true
            model: root.tasks.trashEntries
            currentIndex: -1
            Controls.ScrollBar.vertical: Controls.ScrollBar {}
            delegate: HnListDelegate {
                required property var modelData
                required property int index
                width: entries.width
                title: modelData.name
                subtitle: qsTr("%1  ·  %2  ·  %3").arg(modelData.location).arg(Qt.formatDateTime(modelData.date, Qt.locale().dateTimeFormat(Locale.ShortFormat))).arg(modelData.volume)
                highlighted: entries.currentIndex === index
                onClicked: entries.currentIndex = index
            }
            HnLabel {
                anchors.centerIn: parent
                visible: entries.count === 0
                rawText: root.tasks.trashScanning ? qsTr("Scanning Trash…") : root.tasks.trashProblems.length > 0 ? qsTr("No complete Trash entries found.") : qsTr("Trash is empty.")
            }
        }

        HnLabel {
            Layout.fillWidth: true
            Layout.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            visible: root.tasks.trashProblems.length > 0
            rawText: qsTr("Some Trash locations or entries could not be read.")
            color: HoloniightPalette.error
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.maximumHeight: 105
            visible: root.tasks.trashProblems.length > 0
            Controls.TextArea {
                readOnly: true
                wrapMode: Text.Wrap
                text: root.tasks.trashProblems.join("\n")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            enabled: root.ready && entries.currentIndex >= 0
            Controls.Button {
                text: qsTr("Restore")
                objectName: "restoreTrashButton"
                onClicked: root.tasks.restoreTrash(entries.currentIndex)
            }
            Controls.Button {
                text: qsTr("Restore To…")
                onClicked: root.chooseDestination(entries.currentIndex, root.tasks.trashEntries[entries.currentIndex].name, "")
            }
            Controls.Button {
                text: qsTr("Delete Permanently")
                objectName: "deleteTrashButton"
                onClicked: root.tasks.deleteTrash(entries.currentIndex)
            }
        }
    }

    TrashDialog {
        id: confirmation
        objectName: "trashDeleteConfirmation"
        property var requestId
        title: qsTr("Delete permanently?")
        maximumWidth: 360
        onAccepted: root.tasks.respondToTrashConfirm(true, requestId)
        onRejected: root.tasks.respondToTrashConfirm(false, requestId)
        contentItem: HnLabel {
            wrapMode: Text.Wrap
            rawText: qsTr("Permanently delete %n Trash item(s)?", "", root.tasks.trashConfirmCount)
        }
        footer: Controls.Control {
            leftPadding: confirmation.padding
            rightPadding: confirmation.padding
            bottomPadding: confirmation.padding
            contentItem: RowLayout {
                Item {
                    Layout.fillWidth: true
                }
                Controls.Button {
                    text: qsTr("Cancel")
                    onClicked: confirmation.reject()
                }
                Controls.Button {
                    text: qsTr("Delete Permanently")
                    onClicked: confirmation.accept()
                }
            }
        }
    }

    FolderDialog {
        id: folderPicker
        title: qsTr("Choose an existing destination folder")
        onAccepted: root.chosenFolder = selectedFolder
    }

    TrashDialog {
        id: destination
        objectName: "trashRestoreDestination"
        title: qsTr("Restore To")

        contentItem: ColumnLayout {
            HnLabel {
                Layout.fillWidth: true
                visible: root.restoreReason.length > 0
                rawText: qsTr("Could not restore to the original location. %1").arg(root.restoreReason)
                wrapMode: Text.Wrap
            }
            Controls.Button {
                text: qsTr("Choose Folder…")
                onClicked: folderPicker.open()
            }
            HnLabel {
                visible: root.chosenFolder.toString().length > 0
                rawText: root.chosenFolder.toString()
            }
            Controls.TextField {
                id: destinationName
                objectName: "restoreDestinationName"
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                Accessible.name: qsTr("Name")
            }
        }
        footer: Controls.Control {
            leftPadding: destination.padding
            rightPadding: destination.padding
            bottomPadding: destination.padding
            contentItem: RowLayout {
                Item {
                    Layout.fillWidth: true
                }
                Controls.Button {
                    text: qsTr("Cancel")
                    onClicked: destination.reject()
                }
                Controls.Button {
                    text: qsTr("Restore")
                    enabled: root.chosenFolder.toString().length > 0 && destinationName.text.length > 0 && destinationName.text !== "." && destinationName.text !== ".." && !destinationName.text.includes("/")
                    onClicked: {
                        root.tasks.restoreTrash(root.restoreIndex, root.chosenFolder, destinationName.text);
                        destination.close();
                    }
                }
            }
        }
    }

    component TrashDialog: Controls.Dialog {
        id: dialog
        property real maximumWidth: 460
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        implicitWidth: Math.max(0, Math.min(maximumWidth, (parent?.width ?? 0) - 24))
        padding: HnMetrics.internalSpacing(HnControlSize.Large)
        background: HnSurfaceFrame {
            surfaceRole: HnSurfaceRole.Popup
            fillColor: HoloniightPalette.surfaceRaised
            borderColor: HoloniightPalette.borderFocus
        }
        header: HnLabel {
            padding: dialog.padding
            role: HnTypographyRole.Title
            rawText: dialog.title
        }
    }
}
