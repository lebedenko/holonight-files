pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Window
import Holonight.FileBrowser.Markdown as BrowserMarkdown
import Holonight.FileBrowser.Preview as BrowserPreview
import Holonight.Core
import Holonight.Controls

// The mockup's "Quick look" card: a centered, padded card no larger than 92% of the window, holding
// the preview above a filename / metadata / hint caption. Binds to the *same* controller.preview
// instance the docked PreviewPane uses — no second decode pipeline. The overlay is pinned to the file it
// was opened on: j/k/arrows forwarded to handleKey() move the text viewer's current line (owned by
// PreviewService) and never the listing cursor. This file only renders that state.
Controls.Popup {
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
    readonly property real captionReserve: nameLabel.implicitHeight + root.captionSpacing + metadataLabel.implicitHeight + root.captionSpacing + hintRow.implicitHeight

    QuickLookPresentationModel {
        id: presentation
        objectName: "quickLookPresentation"
        preview: root.preview
        windowSize: root.parent ? Qt.size(root.parent.width, root.parent.height) : Qt.size(0, 0)
        devicePixelRatio: root.parent && root.parent.Window.window ? root.parent.Window.window.devicePixelRatio : 1
        cardPadding: root.cardPadding
        captionReserve: root.captionReserve
        frameCaptionGap: root.frameCaptionGap
        minCompactWidth: root.minCompactWidth
        iconExtent: root.iconExtent
        hintImplicitWidth: hintRow.implicitWidth
    }

    readonly property string metadataText: {
        if (!root.preview.hasEntry)
            return "";
        if (root.preview.mimeType === "inode/directory")
            return qsTr("Dir");
        if (root.preview.quickLookErrorKind !== PreviewService.PreviewErrorKind.None)
            return root.preview.quickLookErrorMessage;
        if (root.preview.mimeType.length === 0)
            return SizeFormat.formatSize(root.preview.size);
        if (root.preview.mimeType.indexOf("image/") === 0) {
            if (root.preview.documentSize.width > 0 && root.preview.documentSize.height > 0)
                return qsTr("%1 × %2 · %3").arg(root.preview.documentSize.width).arg(root.preview.documentSize.height).arg(SizeFormat.formatSize(root.preview.size));
            return SizeFormat.formatSize(root.preview.size);
        }
        if (root.preview.hasText)
            return root.preview.textTruncated ? qsTr("%1 · truncated").arg(SizeFormat.formatSize(root.preview.size)) : SizeFormat.formatSize(root.preview.size);
        if (root.preview.quickLookBusy)
            return SizeFormat.formatSize(root.preview.size);
        return root.preview.mimeTypeDescription.length > 0 ? root.preview.mimeTypeDescription : root.preview.mimeType;
    }
    readonly property color metadataColor: root.preview.quickLookErrorKind !== PreviewService.PreviewErrorKind.None ? HoloniightPalette.error : HoloniightPalette.textMuted

    parent: Controls.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: presentation.cardSize.width
    height: presentation.cardSize.height
    padding: root.cardPadding
    visible: root.controller.quickLookOpen
    modal: true
    focus: true
    closePolicy: Controls.Popup.NoAutoClose

    onOpened: {
        keyContent.forceActiveFocus();
        textViewer.positionViewAtBeginning();
    }

    Controls.Overlay.modal: Rectangle {
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

    // Plain anchors rather than layouts: sizes follow the model synchronously, even while the popup is
    // hidden, instead of waiting for a layout polish on the first frame after opening.
    contentItem: Item {
        id: keyContent
        objectName: "quickLookContent"
        focus: true
        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => {
            if (InspectionKeys.overrideShortcut(event.key, true, false))
                event.accepted = true;
        }
        Keys.onPressed: event => {
            if (markdownLoader.item && (event.key === Qt.Key_J || event.key === Qt.Key_K || event.key === Qt.Key_Up || event.key === Qt.Key_Down) && event.modifiers === Qt.NoModifier) {
                const direction = event.key === Qt.Key_J || event.key === Qt.Key_Down ? 1 : -1;
                (markdownLoader.item as MarkdownScroller).scrollLine(direction);
                event.accepted = true;
                return;
            }
            if (InspectionKeys.press(event.key, event.text, event.modifiers, event.isAutoRepeat, root.controller, true))
                event.accepted = true;
        }
        Keys.onReleased: event => {
            if (InspectionKeys.release(event.key))
                event.accepted = true;
        }

        HnPreviewFrame {
            id: previewFrame
            padding: 0
            previewState: HnPreviewFrame.Content
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: presentation.frameSize.width
            height: presentation.frameSize.height

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
                visible: presentation.kind === QuickLookPresentationModel.Text
                color: HoloniightPalette.background
                border.width: HnMetrics.borderWidth
                border.color: HoloniightPalette.borderSubtle
                radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height, HnAppearance.revision)

                // Vertical-only viewer. Rows have a constant height and a width taken from the view (never from
                // content), the current row is a per-delegate bool rather than a ListView highlight item, and
                // scrolling is imperative — none of these read contentY/contentHeight, so there is no binding loop.
                BrowserPreview.TextPreview {
                    id: textViewer
                    objectName: "quickLookText"
                    anchors.fill: parent
                    anchors.margins: root.cardPadding
                    clip: true
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: false
                    activeFocusOnTab: false
                    Keys.priority: Keys.BeforeItem
                    Keys.forwardTo: [keyContent]
                    currentLineIndex: root.preview.currentLineIndex
                    columnSpacing: root.cardPadding
                    model: root.preview.textLines

                    // Scrolls only when PreviewService moves the line (Contain = minimal scroll), so the mouse
                    // wheel never changes the current line and a j at the last line does not snap the view back.
                    Connections {
                        target: root.preview
                        function onCurrentLineIndexChanged() {
                            if (root.preview.currentLineIndex >= 0)
                                textViewer.positionViewAtIndex(root.preview.currentLineIndex, ListView.Contain);
                        }
                    }

                    Connections {
                        target: root.preview.textLines
                        function onModelReset() {
                            textViewer.positionViewAtBeginning();
                        }
                    }
                }
            }

            Loader {
                id: markdownLoader
                objectName: "quickLookMarkdownLoader"
                anchors.fill: parent
                active: root.controller.quickLookOpen && presentation.markdownFrame
                sourceComponent: MarkdownScroller {}
            }

            HnIcon {
                id: compactIcon
                objectName: "quickLookIcon"
                anchors.centerIn: parent
                size: root.iconExtent
                // A chain the listing or this icon already failed is not requested again (IconFallbacks).
                property string failedChain
                readonly property bool useFallback: IconFallbacks.isUnresolved(root.preview.iconName) || compactIcon.failedChain === root.preview.iconName
                readonly property bool isFolderEntry: root.preview.isDirectory
                source: presentation.kind !== QuickLookPresentationModel.Compact ? "" : !compactIcon.useFallback ? "image://icon/" + root.preview.iconName : compactIcon.isFolderEntry ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
                rendering: compactIcon.useFallback ? HnIcon.Semantic : HnIcon.Original
                visible: presentation.kind === QuickLookPresentationModel.Compact && !root.preview.hasImage
                onHasErrorChanged: if (hasError && !compactIcon.useFallback) {
                    const chain = root.preview.iconName;
                    IconFallbacks.markUnresolved(chain);
                    // Deferred: the failure is reported from inside the source assignment itself.
                    Qt.callLater(() => compactIcon.failedChain = chain);
                }
            }

            Controls.BusyIndicator {
                objectName: "quickLookBusy"
                anchors.centerIn: parent
                width: HnMetrics.iconSize(HnControlSize.Hero)
                height: width
                visible: root.preview.quickLookBusy && !root.preview.hasImage && !root.preview.hasText
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

        HnLabeledKeyHint {
            id: hintRow
            objectName: "quickLookHint"
            anchors.top: metadataLabel.bottom
            anchors.topMargin: root.captionSpacing
            anchors.horizontalCenter: parent.horizontalCenter
            keyGroups: [[Qt.Key_Space], [Qt.Key_Escape]]
            description: qsTr("Close")
            hintItem.objectName: "quickLookCloseKeys"
            descriptionItem.objectName: "quickLookCloseLabel"
            descriptionItem.color: HoloniightPalette.textDisabled
        }
    }

    component MarkdownScroller: Flickable {
        id: markdownScroller
        objectName: "quickLookMarkdown"
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: markdownView.contentHeight + 2 * root.cardPadding
        // Retain the user's position while a refreshed document is being read and laid out.
        property real retainedPosition: 0
        property bool wasBusy: false
        property string loadedSource: ""
        property list<url> authorizedRoots: []
        function syncDocument(): void {
            const busy = root.preview.quickLookBusy;
            if (busy && !wasBusy)
                retainedPosition = contentY;
            wasBusy = busy;
            if (root.preview.hasText) {
                authorizedRoots = root.preview.markdownFileRoots;
                loadedSource = root.preview.markdownSource;
                Qt.callLater(clampPosition);
            }
        }
        Component.onCompleted: syncDocument()
        Connections {
            target: root.preview
            function onChanged() {
                markdownScroller.syncDocument();
            }
        }
        onMovementEnded: retainedPosition = contentY
        onContentYChanged: if (moving)
            retainedPosition = contentY
        function scrollLine(direction: int): void {
            contentY = presentation.markdownScrollTarget(contentY, height, contentHeight, bodyMetrics.height, direction);
            retainedPosition = contentY;
        }
        function clampPosition(): void {
            if (!root.preview.quickLookBusy) {
                contentY = presentation.markdownScrollTarget(retainedPosition, height, contentHeight, 0, 0);
            }
        }
        onContentHeightChanged: Qt.callLater(clampPosition)
        onHeightChanged: Qt.callLater(clampPosition)
        TextMetrics {
            id: bodyMetrics
            font.family: HolonightTheme.uiFont
            font.pointSize: HolonightTheme.bodySize
            text: "Ag"
        }
        BrowserMarkdown.MarkdownDocument {
            id: markdownView
            x: root.cardPadding
            y: root.cardPadding
            width: Math.max(1, markdownScroller.width - 2 * root.cardPadding)
            markdown: markdownScroller.loadedSource
            baseUrl: root.preview.markdownBaseUrl
            authorizedRoots: markdownScroller.authorizedRoots
            bodyFont: bodyMetrics.font
        }
    }
}
