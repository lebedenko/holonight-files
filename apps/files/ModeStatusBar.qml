pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight
import Holonight.Core
import Holonight.Controls

// Replaces the plain path/status label with mode-contextual content (SPEC.md REQ-F-022,
// REQ-F-024, REQ-F-042, REQ-NF-001): the ordinary path/status text in NORMAL, a live selection
// counter in VISUAL, the search query field in SEARCH, and live validation feedback in INSERT.
RowLayout {
    id: root

    required property DirectoryController controller

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    HnLabel {
        objectName: "normalStatusLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
        elide: Text.ElideMiddle
        visible: root.controller.vim.currentMode === VimModeController.Normal && !root.controller.tasks.busy && !root.controller.tasks.hasPrompt
        Layout.fillWidth: visible
        rawText: root.controller.statusMessage.length > 0 ? qsTr("%1  ·  %2").arg(root.controller.currentPath).arg(root.controller.statusMessage) : root.controller.currentPath
    }

    HnLabel {
        objectName: "taskProgressLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
        elide: Text.ElideMiddle
        visible: root.controller.tasks.busy && !root.controller.tasks.hasPrompt
        Layout.fillWidth: visible
        readonly property string operationName: {
            switch (root.controller.tasks.currentOperation) {
            case TaskManager.Move:
                return qsTr("Move");
            case TaskManager.Trash:
                return qsTr("Trash");
            default:
                return qsTr("Copy");
            }
        }
        rawText: qsTr("%1 %2/%3: %4").arg(operationName).arg(root.controller.tasks.itemsDone).arg(root.controller.tasks.itemsTotal).arg(root.controller.tasks.currentItemName)
    }

    HnLabel {
        objectName: "conflictPromptLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.error
        elide: Text.ElideMiddle
        visible: root.controller.tasks.hasPrompt && root.controller.tasks.promptKind === TaskManager.Conflict
        Layout.fillWidth: visible
        rawText: qsTr("%1 already exists as %2 — (s)kip / (o)verwrite / auto-(r)ename / (c)ancel").arg(root.controller.tasks.conflictSourceName).arg(root.controller.tasks.conflictDestName)
    }

    HnLabel {
        objectName: "trashConfirmLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.error
        elide: Text.ElideMiddle
        visible: root.controller.tasks.hasPrompt && root.controller.tasks.promptKind === TaskManager.TrashConfirm
        Layout.fillWidth: visible
        rawText: qsTr("Trash %1 item(s)? (y/n)").arg(root.controller.tasks.trashConfirmCount)
    }

    HnLabel {
        objectName: "visualStatusLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
        visible: root.controller.vim.currentMode === VimModeController.Visual
        Layout.fillWidth: visible
        rawText: qsTr("VISUAL  ·  %1 selected").arg(root.controller.vim.selectedCount)
    }

    TextField {
        id: searchField
        objectName: "searchField"

        visible: root.controller.vim.currentMode === VimModeController.Search
        Layout.fillWidth: visible
        placeholderText: qsTr("Search…")
        text: root.controller.vim.searchQuery

        Connections {
            target: root.controller
            function onChanged(): void {
                if (searchField.visible)
                    searchField.forceActiveFocus();
            }
        }

        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => {
            if (event.key === Qt.Key_Escape)
                event.accepted = true;
        }
        Keys.onReturnPressed: root.controller.commitSearchEditing()
        Keys.onEnterPressed: root.controller.commitSearchEditing()
        Keys.onEscapePressed: root.controller.cancelSearchEditing()

        onTextChanged: if (visible)
            root.controller.updateSearchQuery(text)
        onVisibleChanged: {
            if (visible)
                forceActiveFocus();
            else
                focus = false;
        }
    }

    HnLabel {
        objectName: "insertStatusLabel"
        role: HnTypographyRole.Caption
        color: root.controller.vim.insertValid ? HoloniightPalette.textMuted : HoloniightPalette.error
        elide: Text.ElideMiddle
        visible: root.controller.vim.currentMode === VimModeController.Insert
        Layout.fillWidth: visible
        rawText: root.controller.vim.insertValid ? qsTr("INSERT  ·  Enter to confirm, Esc to cancel") : qsTr("INSERT  ·  %1").arg(root.controller.vim.insertErrorMessage)
    }
}
