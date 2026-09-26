pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

// Replaces the plain path/status label with mode-contextual content (SPEC.md REQ-F-022,
// REQ-F-024, REQ-F-042, REQ-NF-001): the ordinary path/status text in NORMAL, a live selection
// counter in VISUAL, the search query field in SEARCH, and live validation feedback in INSERT.
// A leading pill badge names the active mode (mode-status-badge SPEC.md), so the labels below no
// longer repeat it.
RowLayout {
    id: root

    required property DirectoryController controller

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    // Indexed by VimModeController.Mode (Normal, Visual, Search, Insert); one lookup keeps label,
    // fill and accessible name updating together. Labels are deliberately untranslated.
    readonly property var modeMeta: [
        {
            label: "NORMAL",
            fill: HoloniightPalette.accentBlue
        },
        {
            label: "VISUAL",
            fill: HoloniightPalette.accentViolet
        },
        {
            label: "SEARCH",
            fill: HoloniightPalette.accentYellow
        },
        {
            label: "INSERT",
            fill: HoloniightPalette.success
        }
    ]
    readonly property var currentModeMeta: modeMeta[controller.vim.currentMode]

    Controls.Control {
        objectName: "modeBadge"
        Layout.alignment: Qt.AlignVCenter
        // Sized off the hidden metric so switching modes never moves the labels that follow.
        implicitWidth: Math.ceil(badgeMetric.implicitWidth) + leftPadding + rightPadding
        topPadding: 2
        bottomPadding: 2
        leftPadding: 8
        rightPadding: 8
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("%1 mode").arg(root.currentModeMeta.label)

        contentItem: HnLabel {
            objectName: "modeBadgeLabel"
            role: HnTypographyRole.Code
            color: HoloniightPalette.background
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            rawText: root.currentModeMeta.label
        }
        background: Rectangle {
            color: root.currentModeMeta.fill
            radius: HnAppearance.roundedRadius(HnSurfaceRole.Control, width, height, HnAppearance.revision)
        }
    }

    HnLabel {
        id: badgeMetric
        visible: false
        role: HnTypographyRole.Code
        rawText: "NORMAL"
    }

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
        rawText: qsTr("%1 selected").arg(root.controller.vim.selectedCount)
    }

    Controls.TextField {
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

    RowLayout {
        objectName: "insertGuidance"
        visible: root.controller.vim.currentMode === VimModeController.Insert && root.controller.vim.insertValid
        Layout.fillWidth: visible
        spacing: HnMetrics.internalSpacing(HnControlSize.Compact)

        HnKeyHint {
            objectName: "insertConfirmKeys"
            keyGroups: [[Qt.Key_Return]]
        }
        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: qsTr("Confirm")
        }
        HnKeyHint {
            objectName: "insertCancelKeys"
            Layout.leftMargin: HnMetrics.internalSpacing(HnControlSize.Compact)
            keyGroups: [[Qt.Key_Escape]]
        }
        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: qsTr("Cancel")
        }
        Item {
            Layout.fillWidth: true
        }
    }

    HnLabel {
        objectName: "insertStatusLabel"
        role: HnTypographyRole.Caption
        color: HoloniightPalette.error
        elide: Text.ElideMiddle
        visible: root.controller.vim.currentMode === VimModeController.Insert && !root.controller.vim.insertValid
        Layout.fillWidth: visible
        rawText: root.controller.vim.insertErrorMessage
    }
}
