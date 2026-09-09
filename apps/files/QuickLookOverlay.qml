pragma ComponentBehavior: Bound

import "InspectionKeys.js" as InspectionKeys
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import QtQuick.Window
import Holonight.Core
import Holonight.Controls

// A QtQuick.Controls Popup sized to ~92% of the window and centered (REQ-F-010), binding to the
// *same* controller.preview instance the docked PreviewPane uses — no second decode pipeline.
// j/k forwarded to handleKey() live-update the overlay without closing it (REQ-F-011) because
// cursor movement already flows through DirectoryController::syncPreviewTarget() regardless of
// whether Quick Look is open.
C.Popup {
    id: root

    required property DirectoryController controller

    readonly property PreviewService preview: root.controller.preview

    parent: C.Overlay.overlay
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    width: parent ? parent.width * 0.92 : 0
    height: parent ? parent.height * 0.92 : 0
    visible: root.controller.quickLookOpen
    modal: true
    focus: true
    closePolicy: C.Popup.NoAutoClose

    function reportRequestedSize(): void {
        const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
        root.preview.setRequestedSize(PreviewService.QuickLook, Qt.size(imageArea.width * ratio, imageArea.height * ratio));
    }

    onWidthChanged: root.reportRequestedSize()
    onHeightChanged: root.reportRequestedSize()
    onVisibleChanged: if (root.visible)
        root.reportRequestedSize()

    Component.onCompleted: root.reportRequestedSize()
    Screen.onDevicePixelRatioChanged: root.reportRequestedSize()
    onOpened: keyContent.forceActiveFocus()

    background: Rectangle {
        color: HoloniightPalette.surface
        border.width: HnMetrics.borderWidth
        border.color: HoloniightPalette.borderPassive
    }

    contentItem: ColumnLayout {
        id: keyContent
        objectName: "quickLookContent"
        focus: true
        Keys.priority: Keys.BeforeItem
        Keys.onShortcutOverride: event => InspectionKeys.overrideShortcut(event, true)
        Keys.onPressed: event => InspectionKeys.press(event, root.controller, true)
        Keys.onReleased: event => InspectionKeys.release(event)
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Item {
            id: imageArea
            objectName: "quickLookImageArea"
            onWidthChanged: root.reportRequestedSize()
            onHeightChanged: root.reportRequestedSize()
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.preview.hasImage

            PreviewImageItem {
                anchors.fill: parent
                image: root.preview.image
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.preview.hasText
            clip: true
            contentWidth: width
            contentHeight: quickLookText.implicitHeight

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

        HnEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.preview.hasImage && !root.preview.hasText
            titleText: root.preview.previewErrorKind !== PreviewService.None ? root.preview.previewErrorMessage : qsTr("No preview available")
            descriptionText: root.preview.mimeType
        }

        HnLabel {
            role: HnTypographyRole.Subheading
            font.bold: true
            rawText: root.preview.name
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
    }
}
