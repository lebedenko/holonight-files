pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property DirectoryController controller
    property bool previousQuickLookOpen: false
    property int previousMode: VimModeController.Normal

    // Leading file-type icon cell (SPEC.md REQ-F-008/010); the header's Name label is inset by the
    // same width plus columnSpacing so the two rows stay aligned (REQ-F-009).
    readonly property real iconColumnWidth: 32
    readonly property real sizeColumnWidth: 88
    // Widest expected rendering of "yyyy-MM-dd HH:mm" (all-digit fields, so "9" stands in for the
    // widest glyph in every position); measured against an offstage label using the exact role/
    // font the delegate's own Modified field renders with, rather than re-deriving font metrics.
    readonly property real modifiedColumnWidth: Math.ceil(modifiedColumnMetric.implicitWidth)
    readonly property real columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
    // Every non-gutter column carries this much padding on each side of its content.
    readonly property real cellPadding: 8
    // Breathing room between the selection and the header rule, gutter and right sidebar. The top and
    // bottom gaps are list header/footer items, so they only show at the ends of the scroll range.
    readonly property real listInset: 4
    readonly property real columnPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)
    readonly property bool showSize: width >= lineNumberGutterWidth + 120 + 2 * cellPadding + iconColumnWidth + columnSpacing + sizeColumnWidth + 2 * cellPadding
    readonly property bool showModified: showSize && width >= lineNumberGutterWidth + 120 + 2 * cellPadding + iconColumnWidth + columnSpacing + sizeColumnWidth + modifiedColumnWidth + 4 * cellPadding
    // Vim-hybrid line-number gutter (line-number-gutter SPEC.md REQ-F-004/REQ-C-004): wide enough
    // for at least "999", growing with the row count (listView.count includes an o/O placeholder).
    readonly property int lineNumberGutterDigits: lineNumberGutterDigitsFor(listView.count)
    // Measured text width plus 8px leading and 8px trailing padding; the gutter itself is flush with the sidebar.
    readonly property real lineNumberGutterWidth: Math.ceil(lineNumberGutterMetric.implicitWidth) + 16

    // String length rather than log10 so exact powers of ten never round wrong.
    function lineNumberGutterDigitsFor(rowCount: int): int {
        return Math.max(3, String(rowCount).length);
    }

    HnLabel {
        id: modifiedColumnMetric
        visible: false
        role: HnTypographyRole.Caption
        rawText: "9999-99-99 99:99"
    }

    HnLabel {
        id: lineNumberGutterMetric
        visible: false
        role: HnTypographyRole.Code
        rawText: "9".repeat(root.lineNumberGutterDigits)
    }

    Rectangle {
        id: columnHeader
        objectName: "directoryColumnHeader"
        z: 1
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: HnMetrics.controlHeight(HnControlSize.Compact)
        color: "transparent"
        // Labels elide within their cells. A fractional header clip can truncate the snapped bottom rule.

        RowLayout {
            id: headerCells
            anchors.fill: parent
            // The gutter spacer replaces the leading padding, matching the delegates' leftPadding: 0.
            anchors.leftMargin: 0
            // Header columns track the inset rows below (the delegates start listInset right of the gutter and end listInset before the sidebar).
            anchors.rightMargin: root.listInset
            spacing: 0

            // Blank: no label and no separator above the line-number gutter (REQ-F-009).
            Item {
                objectName: "lineNumberGutterHeader"
                Layout.minimumWidth: root.lineNumberGutterWidth
                Layout.preferredWidth: root.lineNumberGutterWidth
                Layout.maximumWidth: root.lineNumberGutterWidth
                Layout.fillHeight: true
            }
            HnLabel {
                objectName: "nameColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Name")
                elide: Text.ElideRight
                Layout.fillWidth: true
                // Aligned with the delegates' icon, not their filename.
                Layout.leftMargin: root.listInset + root.cellPadding
                Layout.rightMargin: root.cellPadding
            }
            HnLabel {
                id: sizeHeader
                objectName: "sizeColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Size")
                visible: root.showSize
                leftPadding: root.cellPadding
                rightPadding: root.cellPadding
                Layout.minimumWidth: root.sizeColumnWidth + 2 * root.cellPadding
                Layout.preferredWidth: root.sizeColumnWidth + 2 * root.cellPadding
                Layout.maximumWidth: root.sizeColumnWidth + 2 * root.cellPadding
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
            }
            HnLabel {
                id: modifiedHeader
                objectName: "modifiedColumnHeader"
                role: HnTypographyRole.Body
                rawText: qsTr("Modified")
                visible: root.showModified
                leftPadding: root.cellPadding
                rightPadding: root.cellPadding
                Layout.minimumWidth: root.modifiedColumnWidth + 2 * root.cellPadding
                Layout.preferredWidth: root.modifiedColumnWidth + 2 * root.cellPadding
                Layout.maximumWidth: root.modifiedColumnWidth + 2 * root.cellPadding
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
            }
        }

        // The bottom rule owns each junction. Both branches reference its leading boundary.
        HnSeparator {
            objectName: "sizeColumnDivider"
            orientation: Qt.Vertical
            visible: root.showSize
            x: headerCells.x + sizeHeader.x
            anchors.top: parent.top
            anchors.bottom: columnHeaderDivider.top
        }

        HnSeparator {
            objectName: "modifiedColumnDivider"
            orientation: Qt.Vertical
            visible: root.showModified
            x: headerCells.x + modifiedHeader.x
            anchors.top: parent.top
            anchors.bottom: columnHeaderDivider.top
        }

        HnSeparator {
            id: columnHeaderDivider
            objectName: "columnHeaderDivider"
            crossAxisAlignment: HnSeparator.Trailing
            color: HoloniightPalette.borderPassive
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
        }
    }

    // Clips the gutter numbers, which the delegates draw to the left of the list view proper.
    Item {
        id: listArea
        clip: true
        anchors {
            top: columnHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: !root.controller || root.controller.directoryError.length === 0

        // Solid gutter strip: flush with the sidebar, from the header rule to the footer, regardless of row count.
        Rectangle {
            id: gutterStrip
            objectName: "lineNumberGutterStrip"
            color: HoloniightPalette.surfaceRaised
            width: root.lineNumberGutterWidth
            anchors {
                top: parent.top
                bottom: parent.bottom
                left: parent.left
            }
        }

        ListView {
            id: listView
            objectName: "directoryListView"

            anchors {
                top: parent.top
                left: gutterStrip.right
                right: parent.right
                bottom: parent.bottom
                leftMargin: root.listInset
                rightMargin: root.listInset
            }
            header: Item {
                width: listView.width
                height: root.listInset
            }
            footer: Item {
                width: listView.width
                height: root.listInset
            }
            clip: false
            focus: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}
            model: root.controller ? root.controller.listing : null
            currentIndex: root.controller ? root.controller.cursorRow : -1
            onCurrentIndexChanged: {
                if (currentIndex < 0)
                    return;
                // Contain ignores the header/footer inset items, so the ends are positioned explicitly.
                if (currentIndex === 0)
                    positionViewAtBeginning();
                else if (currentIndex === count - 1)
                    positionViewAtEnd();
                else
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
                required property bool isParent
                required property bool isSymlink

                readonly property bool editingThis: root.controller.vim.currentMode === VimModeController.Insert && root.controller.vim.editingRow === delegate.index
                // Name styling precedence (symlink > folder > file): a symlink always renders
                // italic in the secondary color regardless of what it points at; a plain
                // directory renders bold; a regular file is unchanged.
                readonly property bool typeItalic: delegate.isSymlink
                readonly property int typeWeight: !delegate.isSymlink && delegate.isDir ? Font.Bold : Font.Normal
                readonly property color typeColor: delegate.isSymlink ? HoloniightPalette.textSecondary : HoloniightPalette.textPrimary

                objectName: "directoryEntryDelegate"
                width: listView.width
                // The gutter sits flush at the row's left edge; the selection background spans the whole
                // delegate regardless of padding, so it still covers the gutter (REQ-F-005).
                leftPadding: 0
                rightPadding: 0
                highlighted: ListView.isCurrentItem || (root.controller.vim.currentMode === VimModeController.Visual && root.controller.vim.isRowSelected(delegate.index))
                title: name
                subtitle: statFailed ? statError : (isParent ? qsTr("Parent folder") : (isDir ? qsTr("Folder") : Qt.formatDateTime(modified, "yyyy-MM-dd HH:mm")))
                metadata: isDir ? "" : SizeFormat.formatSize(size)
                trailingContent: statFailed ? errorIndicator : null

                // Numbers live in the strip left of the list view; the delegate's own selection fill
                // therefore never covers the gutter.
                HnLabel {
                    readonly property bool isCursorRow: delegate.index === root.controller.cursorRow

                    objectName: "lineNumberGutterField"
                    role: HnTypographyRole.Code
                    textFormat: Text.PlainText
                    font.weight: isCursorRow ? Font.Bold : Font.Normal
                    // Vim hybrid numbering: absolute on the cursor row, relative distance elsewhere.
                    rawText: isCursorRow ? String(delegate.index + 1) : String(Math.abs(delegate.index - root.controller.cursorRow))
                    color: isCursorRow ? HoloniightPalette.accentViolet : HoloniightPalette.textMuted
                    horizontalAlignment: isCursorRow ? Text.AlignLeft : Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    rightPadding: 8
                    x: -(root.lineNumberGutterWidth + root.listInset)
                    width: root.lineNumberGutterWidth
                    height: delegate.height
                    Accessible.ignored: true
                }

                contentItem: RowLayout {
                    spacing: 0
                    Item {
                        id: iconCell
                        objectName: "iconColumnField"
                        Layout.minimumWidth: root.iconColumnWidth
                        Layout.preferredWidth: root.iconColumnWidth
                        Layout.maximumWidth: root.iconColumnWidth
                        Layout.leftMargin: root.cellPadding
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
                        Layout.leftMargin: root.columnSpacing
                        Layout.rightMargin: root.cellPadding
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
                                    color: modelData.matched ? HoloniightPalette.accentCyan : delegate.typeColor
                                    font.weight: modelData.matched ? Font.Bold : delegate.typeWeight
                                    // Italic tracks isSymlink alone, never the search match state
                                    // (REQ-F-009): a symlink's name stays italic in every run.
                                    font.italic: delegate.typeItalic
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
                        visible: root.showSize
                        rightPadding: root.cellPadding
                        leftPadding: root.cellPadding
                        Layout.minimumWidth: root.sizeColumnWidth + 2 * root.cellPadding
                        Layout.preferredWidth: root.sizeColumnWidth + 2 * root.cellPadding
                        Layout.maximumWidth: root.sizeColumnWidth + 2 * root.cellPadding
                    }
                    HnLabel {
                        objectName: "modifiedColumnField"
                        role: HnTypographyRole.Caption
                        rawText: delegate.statFailed ? delegate.statError : (delegate.isParent ? "" : Qt.formatDateTime(delegate.modified, "yyyy-MM-dd HH:mm"))
                        color: delegate.statFailed ? HoloniightPalette.error : HoloniightPalette.textMuted
                        elide: Text.ElideRight
                        visible: root.showModified
                        leftPadding: root.cellPadding
                        rightPadding: root.cellPadding
                        Layout.minimumWidth: root.modifiedColumnWidth + 2 * root.cellPadding
                        Layout.preferredWidth: root.modifiedColumnWidth + 2 * root.cellPadding
                        Layout.maximumWidth: root.modifiedColumnWidth + 2 * root.cellPadding
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
    }

    HnEmptyState {
        objectName: "directoryErrorState"
        anchors.centerIn: parent
        visible: root.controller && root.controller.directoryError.length > 0
        titleText: qsTr("Can't open this folder")
        descriptionText: root.controller ? root.controller.directoryError : ""
    }
}
