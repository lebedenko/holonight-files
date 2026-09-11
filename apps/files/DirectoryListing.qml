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

    readonly property real sizeColumnWidth: 88
    // Widest expected rendering of "yyyy-MM-dd HH:mm" (all-digit fields, so "9" stands in for the
    // widest glyph in every position); measured against an offstage label using the exact role/
    // font the delegate's own Modified field renders with, rather than re-deriving font metrics.
    readonly property real modifiedColumnWidth: Math.ceil(modifiedColumnMetric.implicitWidth)
    readonly property real columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
    readonly property real columnPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)
    readonly property bool showSize: width >= 2 * columnPadding + 120 + columnSpacing + sizeColumnWidth
    readonly property bool showModified: width >= 2 * columnPadding + 120 + 2 * columnSpacing + sizeColumnWidth + modifiedColumnWidth

    HnLabel {
        id: modifiedColumnMetric
        visible: false
        role: HnTypographyRole.Caption
        rawText: "9999-99-99 99:99"
    }

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

    Rectangle {
        id: columnHeader
        objectName: "directoryColumnHeader"
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: HnMetrics.controlHeight(HnControlSize.Compact)
        color: "transparent"
        clip: true

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
            anchors.rightMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
            spacing: root.columnSpacing

            HnLabel {
                objectName: "nameColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Name")
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            HnLabel {
                objectName: "sizeColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Size")
                horizontalAlignment: Text.AlignRight
                visible: root.showSize
                Layout.minimumWidth: root.sizeColumnWidth
                Layout.preferredWidth: root.sizeColumnWidth
                Layout.maximumWidth: root.sizeColumnWidth
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter

                HnSeparator {
                    orientation: Qt.Vertical
                    color: HoloniightPalette.borderPassive
                    x: -(root.columnSpacing + width) / 2
                    height: parent.height
                }
            }
            HnLabel {
                objectName: "modifiedColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Modified")
                visible: root.showModified
                Layout.minimumWidth: root.modifiedColumnWidth
                Layout.preferredWidth: root.modifiedColumnWidth
                Layout.maximumWidth: root.modifiedColumnWidth
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter

                HnSeparator {
                    orientation: Qt.Vertical
                    color: HoloniightPalette.borderPassive
                    x: -(root.columnSpacing + width) / 2
                    height: parent.height
                }
            }
        }

        HnSeparator {
            color: HoloniightPalette.borderPassive
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
        }
    }

    ListView {
        id: listView
        objectName: "directoryListView"

        anchors {
            top: columnHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        focus: true
        ScrollBar.vertical: ScrollBar {}
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
                spacing: root.columnSpacing
                Item {
                    objectName: "nameColumnField"
                    Layout.fillWidth: true
                    implicitHeight: filenameRuns.implicitHeight
                    clip: true
                    Row {
                        id: filenameRuns
                        width: Math.max(0, parent.width - (statErrorIndicator.active ? statErrorIndicator.width + root.columnSpacing : 0))
                        clip: true
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
                    Loader {
                        id: statErrorIndicator
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        active: delegate.statFailed
                        visible: active
                        sourceComponent: errorIndicator
                    }
                }
                HnLabel {
                    objectName: "sizeColumnField"
                    role: HnTypographyRole.Caption
                    rawText: delegate.isDir ? "" : root.formatSize(delegate.size)
                    color: HoloniightPalette.textSecondary
                    horizontalAlignment: Text.AlignRight
                    visible: root.showSize
                    Layout.minimumWidth: root.sizeColumnWidth
                    Layout.preferredWidth: root.sizeColumnWidth
                    Layout.maximumWidth: root.sizeColumnWidth
                }
                HnLabel {
                    objectName: "modifiedColumnField"
                    role: HnTypographyRole.Caption
                    rawText: delegate.statFailed ? delegate.statError : Qt.formatDateTime(delegate.modified, "yyyy-MM-dd HH:mm")
                    color: delegate.statFailed ? HoloniightPalette.error : HoloniightPalette.textMuted
                    elide: Text.ElideRight
                    visible: root.showModified
                    Layout.minimumWidth: root.modifiedColumnWidth
                    Layout.preferredWidth: root.modifiedColumnWidth
                    Layout.maximumWidth: root.modifiedColumnWidth
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
