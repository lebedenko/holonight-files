pragma ComponentBehavior: Bound

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

    // Leading file-type icon cell (SPEC.md REQ-F-008/010); the header's Name label is inset by the
    // same width plus columnSpacing so the two rows stay aligned (REQ-F-009).
    readonly property real iconColumnWidth: 20
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
                Layout.leftMargin: root.iconColumnWidth + root.columnSpacing
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
            required property string iconName

            readonly property bool editingThis: root.controller.vim.currentMode === VimModeController.Insert && root.controller.vim.editingRow === delegate.index

            objectName: "directoryEntryDelegate"
            width: listView.width
            highlighted: ListView.isCurrentItem || (root.controller.vim.currentMode === VimModeController.Visual && root.controller.vim.isRowSelected(delegate.index))
            title: name
            subtitle: statFailed ? statError : (isDir ? qsTr("Folder") : Qt.formatDateTime(modified, "yyyy-MM-dd HH:mm"))
            metadata: isDir ? "" : SizeFormat.formatSize(size)
            trailingContent: statFailed ? errorIndicator : null

            contentItem: RowLayout {
                spacing: root.columnSpacing
                Item {
                    id: iconCell
                    objectName: "iconColumnField"
                    Layout.minimumWidth: root.iconColumnWidth
                    Layout.preferredWidth: root.iconColumnWidth
                    Layout.maximumWidth: root.iconColumnWidth
                    Layout.fillHeight: true

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
                        size: root.iconColumnWidth
                        source: iconCell.skipRequest ? "" : "image://icon/" + delegate.iconName
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
                        size: root.iconColumnWidth
                        source: !iconCell.showFallback ? "" : delegate.isDir ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
                        visible: iconCell.showFallback
                    }
                    // Keep a visible marker even if the packaged SVG cannot be decoded.
                    Rectangle {
                        objectName: "iconFailurePlaceholder"
                        anchors.centerIn: parent
                        width: root.iconColumnWidth
                        height: root.iconColumnWidth
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
                    rawText: delegate.isDir ? "" : SizeFormat.formatSize(delegate.size)
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
