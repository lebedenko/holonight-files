pragma ComponentBehavior: Bound

import "IconFallbacks.js" as IconFallbacks
import "InspectionKeys.js" as InspectionKeys
import "QuickLookGeometry.js" as QuickLookGeometry
import "SizeFormat.js" as SizeFormat
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Window
import Holonight.Core
import Holonight.Controls

// The mockup's "Quick look" card: a centered, padded card no larger than 92% of the window, holding
// the preview above a filename / metadata / hint caption. Binds to the *same* controller.preview
// instance the docked PreviewPane uses — no second decode pipeline. j/k forwarded to handleKey()
// live-update the overlay without closing it because cursor movement already flows through
// DirectoryController::syncPreviewTarget() regardless of whether Quick Look is open.
C.Popup {
    id: root

    required property DirectoryController controller

    readonly property PreviewService preview: root.controller.preview

    readonly property real cardPadding: HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property real captionSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
    readonly property real frameCaptionGap: HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property real minCompactWidth: HnMetrics.controlHeight(HnControlSize.Hero) * 4
    readonly property real iconExtent: HnMetrics.iconSize(HnControlSize.Hero) * 4
    // The caption labels elide and never wrap, so their implicit heights are one line each whatever
    // the entry: the reserve, and with it the decode request size, stay constant across navigation.
    readonly property real captionReserve: nameLabel.implicitHeight + root.captionSpacing + metadataLabel.implicitHeight + root.captionSpacing + hintLabel.implicitHeight

    readonly property string kind: QuickLookGeometry.classify(root.preview.hasEntry, root.preview.busy, root.preview.mimeType, root.preview.hasText, root.preview.previewErrorKind !== PreviewService.None)

    // Written only by settle(), never bound to the card's own size (REQ-NF-001). A pending entry keeps
    // previous layout inputs so navigation keeps its size and window resizing can refit it (REQ-F-013).
    property string settledKind: "none"
    property size settledSourcePixelSize: Qt.size(0, 0)
    property real settledWidth: 0
    property real settledHeight: 0
    property real settledFrameWidth: 0
    property real settledFrameHeight: 0

    // Test-observable count of setRequestedSize() calls (REQ-F-018).
    property int requestedSizeCallCount: 0

    readonly property string metadataText: {
        if (!root.preview.hasEntry)
            return "";
        if (root.preview.mimeType === "inode/directory")
            return qsTr("Dir");
        if (root.preview.previewErrorKind !== PreviewService.None)
            return root.preview.previewErrorMessage;
        if (root.preview.mimeType.length === 0)
            return SizeFormat.formatSize(root.preview.size);
        if (root.preview.mimeType.indexOf("image/") === 0) {
            if (root.preview.sourcePixelSize.width > 0 && root.preview.sourcePixelSize.height > 0)
                return qsTr("%1 × %2 · %3").arg(root.preview.sourcePixelSize.width).arg(root.preview.sourcePixelSize.height).arg(SizeFormat.formatSize(root.preview.size));
            return SizeFormat.formatSize(root.preview.size);
        }
        if (root.preview.hasText)
            return SizeFormat.formatSize(root.preview.size) + (root.preview.textTruncated ? qsTr(" · truncated") : "");
        if (root.preview.busy)
            return SizeFormat.formatSize(root.preview.size);
        return root.preview.mimeTypeDescription.length > 0 ? root.preview.mimeTypeDescription : root.preview.mimeType;
    }
    readonly property color metadataColor: root.preview.previewErrorKind !== PreviewService.None ? HoloniightPalette.error : HoloniightPalette.textMuted

    // Imperative, so each target change moves the card at most once and in one step (REQ-F-013).
    function settle(): void {
        let kind = QuickLookGeometry.classify(root.preview.hasEntry, root.preview.busy, root.preview.mimeType, root.preview.hasText, root.preview.previewErrorKind !== PreviewService.None);
        let sourceSize = root.preview.sourcePixelSize;
        if (kind === "pending" || kind === "none") {
            // Retain the layout inputs, not stale pixel extents: a resize must also refit the frame.
            kind = root.settledKind === "none" ? "compact" : root.settledKind;
            sourceSize = root.settledSourcePixelSize;
        }
        const boundsWidth = root.parent ? root.parent.width * 0.92 : 0;
        const boundsHeight = root.parent ? root.parent.height * 0.92 : 0;
        if (kind === "compact") {
            root.settledFrameWidth = root.iconExtent;
            root.settledFrameHeight = root.iconExtent;
            root.settledWidth = Math.floor(QuickLookGeometry.compactCardWidth(root.minCompactWidth, hintLabel.implicitWidth, root.iconExtent, root.cardPadding, boundsWidth));
            root.settledHeight = Math.floor(Math.min(2 * root.cardPadding + root.iconExtent + root.frameCaptionGap + root.captionReserve, boundsHeight));
        } else {
            const previewBoundsWidth = Math.max(0, boundsWidth - 2 * root.cardPadding);
            const previewBoundsHeight = Math.max(0, boundsHeight - 2 * root.cardPadding - root.frameCaptionGap - root.captionReserve);
            let frameWidth = previewBoundsWidth;
            let frameHeight = previewBoundsHeight;
            if (kind === "image" && sourceSize.width > 0 && sourceSize.height > 0) {
                const fitted = QuickLookGeometry.fitRect(sourceSize.width, sourceSize.height, previewBoundsWidth, previewBoundsHeight);
                frameWidth = fitted.width;
                frameHeight = fitted.height;
            }
            // Whole logical pixels keep the card edges and image clip crisp at fractional scales.
            root.settledFrameWidth = Math.floor(frameWidth);
            root.settledFrameHeight = Math.floor(frameHeight);
            // A narrow portrait still gets a readable caption; its frame centers in the wider card.
            root.settledWidth = Math.floor(Math.min(boundsWidth, Math.max(root.settledFrameWidth + 2 * root.cardPadding, root.minCompactWidth)));
            root.settledHeight = Math.floor(Math.min(boundsHeight, root.settledFrameHeight + 2 * root.cardPadding + root.frameCaptionGap + root.captionReserve));
        }
        root.settledKind = kind;
        root.settledSourcePixelSize = sourceSize;
    }

    // From the window size alone, never from the fitted frame or the card, so navigating between
    // entries of different shapes never re-requests a decode size (REQ-F-017/018).
    function reportRequestedSize(): void {
        const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
        const boundsWidth = root.parent ? root.parent.width * 0.92 : 0;
        const boundsHeight = root.parent ? root.parent.height * 0.92 : 0;
        const width = Math.max(0, boundsWidth - 2 * root.cardPadding);
        const height = Math.max(0, boundsHeight - 2 * root.cardPadding - root.frameCaptionGap - root.captionReserve);
        root.preview.setRequestedSize(PreviewService.QuickLook, Qt.size(Math.round(width * ratio), Math.round(height * ratio)));
        root.requestedSizeCallCount += 1;
    }

    parent: C.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    // Clamped so a resize while an entry is still pending cannot push the card past the bounds.
    width: parent ? Math.min(root.settledWidth, Math.floor(parent.width * 0.92)) : 0
    height: parent ? Math.min(root.settledHeight, Math.floor(parent.height * 0.92)) : 0
    padding: root.cardPadding
    visible: root.controller.quickLookOpen
    modal: true
    focus: true
    closePolicy: C.Popup.NoAutoClose

    Component.onCompleted: {
        root.settle();
        root.reportRequestedSize();
    }
    Screen.onDevicePixelRatioChanged: root.reportRequestedSize()
    onCaptionReserveChanged: {
        root.settle();
        root.reportRequestedSize();
    }
    onOpened: keyContent.forceActiveFocus()

    Connections {
        target: root.preview
        function onChanged(): void {
            root.settle();
        }
    }

    Connections {
        target: root.parent
        function onWidthChanged(): void {
            root.settle();
            root.reportRequestedSize();
        }
        function onHeightChanged(): void {
            root.settle();
            root.reportRequestedSize();
        }
    }

    C.Overlay.modal: Rectangle {
        objectName: "quickLookBackdrop"
        color: HoloniightPalette.scrim
    }

    background: Rectangle {
        objectName: "quickLookCard"
        color: HoloniightPalette.surface
        border.width: HnMetrics.borderWidth
        border.color: HoloniightPalette.borderPassive
        radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height, HnAppearance.revision)
    }

    // Plain anchors rather than layouts: sizes follow settle() synchronously, even while the popup is
    // hidden, instead of waiting for a layout polish on the first frame after opening.
    contentItem: Item {
        id: keyContent
        objectName: "quickLookContent"
        focus: true
        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, true)
        Keys.onPressed: event => InspectionKeys.press(event, root.controller, true)
        Keys.onReleased: event => InspectionKeys.release(event)

        Item {
            id: previewFrame
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.settledFrameWidth
            height: root.settledFrameHeight

            Item {
                id: imageArea
                objectName: "quickLookImageArea"
                anchors.fill: parent
                visible: root.preview.hasImage

                PreviewImageItem {
                    anchors.fill: parent
                    image: root.preview.image
                    radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height, HnAppearance.revision)
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: root.preview.hasText
                color: HoloniightPalette.background
                border.width: HnMetrics.borderWidth
                border.color: HoloniightPalette.borderSubtle
                radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height, HnAppearance.revision)

                Flickable {
                    anchors.fill: parent
                    anchors.margins: root.cardPadding
                    clip: true
                    contentWidth: width
                    contentHeight: quickLookText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    TextEdit {
                        id: quickLookText
                        objectName: "quickLookText"
                        Keys.priority: Keys.BeforeItem
                        Keys.forwardTo: [keyContent]
                        width: parent.width
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        text: root.preview.textContent
                        font.family: HolonightTheme.monospaceFont
                        font.pointSize: HolonightTheme.monospaceFontSize
                        color: HoloniightPalette.textPrimary
                    }
                }
            }

            HnIcon {
                id: compactIcon
                objectName: "quickLookIcon"
                anchors.centerIn: parent
                size: root.iconExtent
                // A chain the listing or this icon already failed is not requested again (IconFallbacks.js).
                property string failedChain
                readonly property bool useFallback: IconFallbacks.isUnresolved(root.preview.iconName) || compactIcon.failedChain === root.preview.iconName
                readonly property bool isFolderIconName: root.preview.iconName === "folder" || root.preview.iconName.startsWith("folder/")
                source: root.kind !== "compact" ? "" : !compactIcon.useFallback ? "image://icon/" + root.preview.iconName : compactIcon.isFolderIconName ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
                visible: root.kind === "compact" && !root.preview.hasImage
                onHasErrorChanged: if (hasError && !compactIcon.useFallback) {
                    const chain = root.preview.iconName;
                    IconFallbacks.markUnresolved(chain);
                    // Deferred: the failure is reported from inside the source assignment itself.
                    Qt.callLater(() => compactIcon.failedChain = chain);
                }
            }

            C.BusyIndicator {
                objectName: "quickLookBusy"
                anchors.centerIn: parent
                width: HnMetrics.iconSize(HnControlSize.Hero)
                height: width
                visible: root.preview.busy && !root.preview.hasImage && !root.preview.hasText
                running: visible
            }
        }

        HnLabel {
            id: nameLabel
            objectName: "quickLookName"
            role: HnTypographyRole.Subheading
            font.bold: true
            color: HoloniightPalette.textPrimary
            rawText: root.preview.name
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignHCenter
            anchors.top: previewFrame.bottom
            anchors.topMargin: root.frameCaptionGap
            anchors.left: parent.left
            anchors.right: parent.right
        }

        HnLabel {
            id: metadataLabel
            objectName: "quickLookMetadata"
            role: HnTypographyRole.Caption
            color: root.metadataColor
            rawText: root.metadataText
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            anchors.top: nameLabel.bottom
            anchors.topMargin: root.captionSpacing
            anchors.left: parent.left
            anchors.right: parent.right
        }

        HnLabel {
            id: hintLabel
            objectName: "quickLookHint"
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textDisabled
            rawText: qsTr("Press Space or Esc to close")
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            anchors.top: metadataLabel.bottom
            anchors.topMargin: root.captionSpacing
            anchors.left: parent.left
            anchors.right: parent.right
            onImplicitWidthChanged: root.settle()
        }
    }
}
