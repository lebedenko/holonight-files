pragma ComponentBehavior: Bound

import "InspectionKeys.js" as InspectionKeys
import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight
import Holonight.Controls

Item {
    id: root

    required property DirectoryController controller
    property bool previousQuickLookOpen: false
    property int previousMode: VimModeController.Normal

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
        Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, false, root.controller.vim.currentMode === VimModeController.Visual)
        Keys.onPressed: event => InspectionKeys.press(event, root.controller, false)
        Keys.onReleased: event => InspectionKeys.release(event)
        Connections {
            target: root.controller
            function onNavigated(): void {
                listView.forceActiveFocus();
            }
            function onChanged(): void {
                if (root.previousQuickLookOpen && !root.controller.quickLookOpen)
                    listView.forceActiveFocus();
                root.previousQuickLookOpen = root.controller.quickLookOpen;

                const mode = root.controller.vim.currentMode;
                if (root.previousMode !== VimModeController.Normal && mode === VimModeController.Normal) {
                    listView.forceActiveFocus();
                    Qt.callLater(() => {
                        if (root.controller.vim.currentMode === VimModeController.Normal)
                            listView.forceActiveFocus();
                    });
                }
                root.previousMode = mode;
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

            readonly property bool editingThis: root.controller.vim.currentMode === VimModeController.Insert && root.controller.vim.editingRow === delegate.index

            objectName: "directoryEntryDelegate"
            width: listView.width
            highlighted: ListView.isCurrentItem || (root.controller.vim.currentMode === VimModeController.Visual && root.controller.vim.isRowSelected(delegate.index))
            title: name
            subtitle: statFailed ? statError : (isDir ? qsTr("Folder") : Qt.formatDateTime(modified, "yyyy-MM-dd HH:mm"))
            metadata: isDir ? "" : root.formatSize(size)
            trailingContent: statFailed ? errorIndicator : null

            contentItem: RowLayout {
                spacing: delegate.semanticSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Math.max(2, delegate.semanticSpacing / 2)
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: filenameRuns.implicitHeight
                        clip: true
                        Row {
                            id: filenameRuns
                            Repeater {
                                model: {
                                    const positions = root.controller.vim.currentMode === VimModeController.Search && root.controller.cursorRow === delegate.index ? root.controller.vim.searchMatchPositions : [];
                                    const runs = [];
                                    for (let i = 0; i < delegate.name.length; ++i) {
                                        const matched = positions.indexOf(i) !== -1;
                                        if (runs.length && runs[runs.length - 1].matched === matched)
                                            runs[runs.length - 1].text += delegate.name[i];
                                        else
                                            runs.push({
                                                text: delegate.name[i],
                                                matched: matched
                                            });
                                    }
                                    return runs;
                                }
                                HnLabel {
                                    required property var modelData
                                    objectName: "filenameRun"
                                    role: HnTypographyRole.Body
                                    rawText: modelData.text
                                    textFormat: Text.PlainText
                                    color: modelData.matched ? HoloniightPalette.accentCyan : HoloniightPalette.textPrimary
                                    font.weight: modelData.matched ? Font.Bold : Font.Normal
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }
                    HnLabel {
                        Layout.fillWidth: true
                        role: HnTypographyRole.Caption
                        rawText: delegate.subtitle
                        color: HoloniightPalette.textMuted
                        elide: Text.ElideRight
                        visible: rawText.length > 0
                    }
                }
                HnLabel {
                    role: HnTypographyRole.Caption
                    rawText: delegate.metadata
                    color: HoloniightPalette.textSecondary
                    visible: rawText.length > 0
                    Layout.alignment: Qt.AlignTop
                }
                Loader {
                    active: delegate.statFailed
                    visible: active
                    sourceComponent: errorIndicator
                }
            }

            Component {
                id: errorIndicator

                HnStatusIndicator {
                    status: HnStatusIndicator.Warning
                }
            }

            Keys.priority: Keys.BeforeItem
            Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, false, root.controller.vim.currentMode === VimModeController.Visual)
            Keys.onPressed: event => InspectionKeys.press(event, root.controller, false)
            Keys.onReleased: event => InspectionKeys.release(event)

            onClicked: root.controller.openEntry(delegate.index)

            // INSERT-mode inline rename/create editor (SPEC.md REQ-F-006 through REQ-F-017): an
            // opaque-background TextField overlaid on this delegate, covering its label, rather
            // than a separate floating popup — it scrolls/clips with the delegate for free
            // (docs/sdd/vim-modal-editing/DESIGN.md).
            TextField {
                id: inlineEditor
                objectName: "inlineNameEditor"

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 8
                visible: delegate.editingThis
                text: root.controller.vim.insertText
                hasError: !root.controller.vim.insertValid

                Keys.priority: Keys.BeforeItem
                Keys.onShortcutOverride: event => {
                    if (event.key === Qt.Key_Escape)
                        event.accepted = true;
                }
                Keys.onReturnPressed: root.controller.commitInsertEditing()
                Keys.onEnterPressed: root.controller.commitInsertEditing()
                Keys.onEscapePressed: root.controller.cancelInsertEditing()

                onTextChanged: if (delegate.editingThis)
                    root.controller.updateInsertText(text)
                onVisibleChanged: if (visible) {
                    forceActiveFocus();
                    Qt.callLater(() => {
                        if (delegate.editingThis) {
                            forceActiveFocus();
                            cursorPosition = root.controller.vim.insertCursorPosition;
                        }
                    });
                } else {
                    focus = false;
                }
            }
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
