pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Holonight.Core
import Holonight.Controls

// Docked sidebar, always visible (REQ-F-001), reactive purely through property bindings to
// controller.preview. Shares its decoder and cached thumbnail data with QuickLookOverlay.qml —
// both bind to the same controller.preview instance rather than owning a second pipeline.
Item {
    id: root

    required property DirectoryController controller

    readonly property PreviewService preview: root.controller.preview

    function formatSize(bytes: real): string {
        if (bytes < 0)
            return qsTr("Unavailable");
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes.toFixed(0));
        const units = ["KiB", "MiB", "GiB", "TiB"];
        let value = bytes / 1024;
        let unitIndex = 0;
        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024;
            unitIndex += 1;
        }
        return qsTr("%1 %2 (%3 bytes)").arg(value.toFixed(1)).arg(units[unitIndex]).arg(bytes.toFixed(0));
    }

    function reportImageAreaSize(): void {
        const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
        root.preview.setRequestedSize(PreviewService.Pane, Qt.size(imageArea.width * ratio, imageArea.height * ratio));
    }

    Component.onCompleted: root.reportImageAreaSize()
    Screen.onDevicePixelRatioChanged: root.reportImageAreaSize()

    function formatModified(value): string {
        if (!value || isNaN(value.getTime()))
            return qsTr("Unavailable");
        return value.toLocaleString(Qt.locale(), Locale.ShortFormat) || value.toISOString();
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)
        visible: root.preview.hasEntry

        Item {
            id: imageArea
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width, 240)
            visible: root.preview.hasImage
            onWidthChanged: root.reportImageAreaSize()
            onHeightChanged: root.reportImageAreaSize()

            PreviewImageItem {
                anchors.fill: parent
                image: root.preview.image
            }
        }

        HnLabel {
            role: HnTypographyRole.Subheading
            font.bold: true
            rawText: root.preview.name
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: qsTr("%1  ·  %2  ·  %3").arg(root.formatSize(root.preview.size)).arg(root.formatModified(root.preview.modified)).arg(root.preview.permissions)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: root.preview.mimeType
            visible: text.length > 0
            Layout.fillWidth: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: root.preview.exifPresent

            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("Camera: %1 %2").arg(root.preview.exifMake).arg(root.preview.exifModel)
                visible: root.preview.exifMake.length > 0 || root.preview.exifModel.length > 0
                Layout.fillWidth: true
            }
            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("Exposure: %1").arg(root.preview.exifExposureTime)
                visible: root.preview.exifExposureTime.length > 0
                Layout.fillWidth: true
            }
            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("Focal length: %1").arg(root.preview.exifFocalLength)
                visible: root.preview.exifFocalLength.length > 0
                Layout.fillWidth: true
            }
            HnLabel {
                role: HnTypographyRole.Caption
                rawText: qsTr("ISO: %1").arg(root.preview.exifIso)
                visible: root.preview.exifIso.length > 0
                Layout.fillWidth: true
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.preview.hasText
            clip: true
            contentWidth: width
            contentHeight: textContent.implicitHeight

            TextEdit {
                id: textContent
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

        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: qsTr("Showing the first part of a larger file")
            visible: root.preview.textTruncated
            Layout.fillWidth: true
        }

        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textMuted
            rawText: qsTr("Loading…")
            visible: root.preview.busy
            Layout.fillWidth: true
        }

        HnLabel {
            role: HnTypographyRole.Caption
            color: HoloniightPalette.textAccent
            rawText: root.preview.previewErrorMessage
            visible: root.preview.previewErrorKind !== PreviewService.None
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    HnEmptyState {
        anchors.centerIn: parent
        width: parent.width - (2 * HnMetrics.internalSpacing(HnControlSize.Normal))
        visible: !root.preview.hasEntry
        titleText: qsTr("No selection")
        descriptionText: qsTr("Select a file or folder to see its details here.")
    }
}
