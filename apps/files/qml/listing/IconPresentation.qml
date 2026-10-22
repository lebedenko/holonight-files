pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls
import Holonight.FileBrowser as Browser

Item {
    id: root
    required property DirectoryController controller
    property bool previousQuickLookOpen: false
    property int previousMode: VimModeController.Normal
    function focusListing(): void {
        listView.forceActiveFocus(Qt.TabFocusReason);
    }
    Item {
        id: dragProxy
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.proposedAction: Qt.CopyAction
    }
    Browser.DirectoryGrid {
        id: listView
        objectName: "directoryGridView"
        anchors.fill: parent
        visible: root.controller.directoryError.length === 0
        model: root.controller.listing
        currentIndex: root.controller.cursorRow
        readonly property int columns: Math.max(1, Math.floor(width / 128))
        readonly property int visibleRows: Math.max(1, Math.floor(height / cellHeight))
        cellWidth: width / columns
        cellHeight: 128
        onColumnsChanged: root.controller.setGridDimensions(columns, visibleRows)
        onVisibleRowsChanged: root.controller.setGridDimensions(columns, visibleRows)
        onCurrentIndexChanged: if (currentIndex >= 0)
            positionViewAtIndex(currentIndex, GridView.Contain)
        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => {
            if (InspectionKeys.overrideShortcut(event.key, false, root.controller.vim.currentMode === VimModeController.Visual))
                event.accepted = true;
        }
        Keys.onPressed: event => {
            if (InspectionKeys.press(event.key, event.text, event.modifiers, event.isAutoRepeat, root.controller, false))
                event.accepted = true;
        }
        Keys.onReleased: event => {
            if (InspectionKeys.release(event.key))
                event.accepted = true;
        }
        Connections {
            target: root.controller
            function onNavigated(): void {
                // j/k inside Quick Look also reach a new file; focus must stay in the modal popup.
                if (!root.controller.quickLookOpen)
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
        Component.onCompleted: {
            forceActiveFocus();
            root.controller.setGridDimensions(columns, visibleRows);
        }

        delegate: HnListDelegate {
            id: delegate
            required property int index
            required property string name
            required property string iconName
            required property bool isDir
            required property bool isSymlink
            required property bool statFailed
            required property string statError
            readonly property bool editingThis: root.controller.vim.currentMode === VimModeController.Insert && root.controller.vim.editingRow === index
            objectName: "directoryIconDelegate"
            focusPolicy: root.controller.transfersEnabled ? Qt.StrongFocus : Qt.NoFocus
            Accessible.name: name
            width: listView.cellWidth
            height: listView.cellHeight
            leftPadding: 4
            rightPadding: 4
            topPadding: 0
            bottomPadding: 0
            highlighted: GridView.isCurrentItem || (root.controller.vim.currentMode === VimModeController.Visual && root.controller.vim.isRowSelected(index))
            contentItem: Item {
                Item {
                    id: iconCell
                    objectName: "iconColumnField"
                    width: 64
                    height: 64
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 8

                    // Deliberately not reactive to later failures: it only spares rows created after
                    // an earlier row's miss from repeating the request (IconFallbacks).
                    readonly property bool knownUnresolved: IconFallbacks.isUnresolved(delegate.iconName)
                    // Once this row's own request fails, stop re-requesting (e.g. on a device pixel
                    // ratio change) until the row's chain itself changes.
                    property string failedChain
                    readonly property bool skipRequest: knownUnresolved || failedChain === delegate.iconName
                    readonly property bool showFallback: skipRequest || themeIcon.hasError

                    // Theme icon, untinted in its own colours (REQ-F-003). The chain in iconName is
                    // walked by the C++ image provider; a total miss surfaces as hasError.
                    HnIcon {
                        id: themeIcon
                        objectName: "themeFileIcon"
                        anchors.centerIn: parent
                        size: 64
                        source: iconCell.skipRequest ? "" : "image://icon/" + delegate.iconName
                        rendering: HnIcon.Original
                        visible: !iconCell.showFallback
                        onHasErrorChanged: if (hasError) {
                            const chain = delegate.iconName;
                            IconFallbacks.markUnresolved(chain);
                            // Deferred: the failure is reported from inside the source assignment itself.
                            Qt.callLater(() => iconCell.failedChain = chain);
                        }
                    }
                    // Bundled glyph, tinted with the palette (REQ-F-016/017); sourced only after a miss.
                    HnIcon {
                        id: fallbackIcon
                        objectName: "fallbackFileIcon"
                        anchors.centerIn: parent
                        size: 64
                        source: !iconCell.showFallback ? "" : delegate.isDir ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
                        visible: iconCell.showFallback
                    }
                    // Keep a visible marker even if the packaged SVG cannot be decoded.
                    Rectangle {
                        objectName: "iconFailurePlaceholder"
                        anchors.centerIn: parent
                        width: 64
                        height: 64
                        radius: 3
                        color: "transparent"
                        border.color: HoloniightPalette.textMuted
                        visible: iconCell.showFallback && fallbackIcon.hasError
                        Text {
                            anchors.centerIn: parent
                            text: "?"
                            color: HoloniightPalette.textMuted
                            font.pixelSize: 14
                        }
                    }
                }

                HnLabel {
                    anchors.top: iconCell.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 4
                    rawText: {
                        const positions = root.controller.vim.currentMode === VimModeController.Search && root.controller.cursorRow === delegate.index ? root.controller.vim.searchMatchPositions : [];
                        let text = "";
                        for (let i = 0; i < delegate.name.length; ++i) {
                            const escaped = delegate.name[i].replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
                            text += positions.indexOf(i) < 0 ? escaped : '<b><font color="' + HoloniightPalette.accentCyan + '">' + escaped + '</font></b>';
                        }
                        return text;
                    }
                    textFormat: Text.StyledText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.bold: delegate.isDir && !delegate.isSymlink
                    font.italic: delegate.isSymlink
                    color: delegate.isSymlink ? HoloniightPalette.textSecondary : HoloniightPalette.textPrimary
                }
                HnStatusIndicator {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    visible: delegate.statFailed
                    status: HnStatusIndicator.Warning
                }
            }
            Keys.priority: Keys.BeforeItem
            Keys.onShortcutOverride: event => {
                if (InspectionKeys.overrideShortcut(event.key, false, root.controller.vim.currentMode === VimModeController.Visual))
                    event.accepted = true;
            }
            Keys.onPressed: event => {
                if (InspectionKeys.press(event.key, event.text, event.modifiers, event.isAutoRepeat, root.controller, false))
                    event.accepted = true;
            }
            Keys.onReleased: event => {
                if (InspectionKeys.release(event.key))
                    event.accepted = true;
            }

            DragHandler {
                id: rowDrag
                target: null
                acceptedButtons: Qt.LeftButton
                enabled: root.controller.transfersEnabled
                onActiveChanged: if (active) {
                    const payload = root.controller.dragUriList(delegate.index);
                    if (payload.length > 0) {
                        dragProxy.Drag.mimeData = {
                            "text/uri-list": payload
                        };
                        dragProxy.Drag.active = true;
                    }
                } else {
                    dragProxy.Drag.active = false;
                }
            }

            onClicked: if (!rowDrag.active)
                root.controller.selectEntry(index)
            onDoubleClicked: root.controller.activateEntry(index)
            Controls.TextField {
                id: inlineEditor
                objectName: "inlineNameEditor"

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                // The gutter lies outside the list view, so line numbers stay visible while editing (REQ-F-011).
                anchors.rightMargin: 8
                visible: delegate.editingThis
                text: root.controller.vim.insertText
                Binding {
                    target: inlineEditor
                    property: "hasError"
                    value: !root.controller.vim.insertValid
                    when: "hasError" in inlineEditor
                }

                Keys.priority: Keys.BeforeItem
                Keys.onShortcutOverride: event => {
                    if (event.key === Qt.Key_Escape)
                        event.accepted = true;
                }
                Keys.onReturnPressed: root.controller.commitInsertEditing()
                Keys.onEnterPressed: root.controller.commitInsertEditing()
                Keys.onEscapePressed: root.controller.cancelInsertEditing()

                // Controller-driven binding updates must not echo back and emit changed again.
                onTextChanged: if (delegate.editingThis && text !== root.controller.vim.insertText)
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
    FileDropArea {
        id: dropArea
        objectName: "fileDropArea"
        anchors.fill: listView
        clip: true
        controller: root.controller
        enabled: root.controller.transfersEnabled
        function updateTarget(): void {
            const point = listView.mapFromItem(dropArea, position.x, position.y);
            targetRow = listView.indexAt(point.x + listView.contentX, point.y + listView.contentY);
        }
        onFeedbackChanged: if (hovering)
            updateTarget()
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: dropArea.hovering ? 2 : 0
            border.color: dropArea.action === Qt.IgnoreAction ? HoloniightPalette.textMuted : HoloniightPalette.accentCyan
        }
        HnLabel {
            visible: dropArea.hovering
            x: Math.max(0, Math.min(dropArea.width - width, dropArea.position.x + 12))
            y: Math.max(0, Math.min(dropArea.height - height, dropArea.position.y + 12))
            rawText: dropArea.action === Qt.CopyAction ? qsTr("Copy here") : dropArea.action === Qt.MoveAction ? qsTr("Move here") : qsTr("Can't drop here")
        }
        Rectangle {
            readonly property var target: dropArea.targetRow >= 0 ? listView.itemAtIndex(dropArea.targetRow) : null
            visible: dropArea.hovering && target !== null && root.controller.dropDestination(dropArea.targetRow) !== root.controller.currentPath
            x: target ? target.x : 0
            y: target ? target.y - listView.contentY : 0
            width: target ? target.width : 0
            height: target ? target.height : 0
            color: "transparent"
            border.width: 2
            border.color: dropArea.action === Qt.IgnoreAction ? HoloniightPalette.textMuted : HoloniightPalette.accentCyan
        }
        Timer {
            interval: 75
            repeat: true
            running: dropArea.hovering && dropArea.enabled
            onTriggered: {
                const direction = dropArea.position.y < 32 ? -1 : dropArea.position.y > dropArea.height - 32 ? 1 : 0;
                const minimum = listView.originY;
                const maximum = Math.max(minimum, minimum + listView.contentHeight - listView.height);
                listView.contentY = Math.max(minimum, Math.min(maximum, listView.contentY + direction * HnMetrics.controlHeight(HnControlSize.Compact)));
                dropArea.updateTarget();
            }
        }
    }

    HnEmptyState {
        anchors.centerIn: parent
        visible: root.controller.directoryError.length > 0
        titleText: qsTr("Can't open this folder")
        descriptionText: root.controller.directoryError
    }
}
