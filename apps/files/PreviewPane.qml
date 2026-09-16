pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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

    // Height comes from the frame's own width and the source aspect ratio, never read back from
    // imageArea.height: that feedback edge is what would turn the aspect-ratio frame into a
    // binding loop (DESIGN.md §5.1).
    function reportImageAreaSize(): void {
        const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
        root.preview.setRequestedSize(PreviewService.Pane, Qt.size(imageArea.width * ratio, imageArea.frameHeight * ratio));
    }

    Component.onCompleted: root.reportImageAreaSize()
    Screen.onDevicePixelRatioChanged: root.reportImageAreaSize()

    function formatModified(value): string {
        if (!value || isNaN(value.getTime()))
            return qsTr("Unavailable");
        return value.toLocaleString(Qt.locale(), Locale.ShortFormat) || value.toISOString();
    }

    // Row values; an empty string hides the row entirely (SPEC.md REQ-F-008/012).
    readonly property string sizeText: root.preview.mimeType === "inode/directory" ? qsTr("Dir") : SizeFormat.formatSize(root.preview.size)
    readonly property string dimensionsText: root.preview.sourcePixelSize.width > 0 && root.preview.sourcePixelSize.height > 0 ? qsTr("%1 \u00d7 %2").arg(root.preview.sourcePixelSize.width).arg(root.preview.sourcePixelSize.height) : ""
    // formatModified()'s "Unavailable" sentinel hides the row rather than rendering (DESIGN.md
    // conflict item 1).
    readonly property string modifiedText: {
        const formatted = root.formatModified(root.preview.modified);
        return formatted === qsTr("Unavailable") ? "" : formatted;
    }
    readonly property string cameraText: [root.preview.exifMake, root.preview.exifModel].filter(part => part.length > 0).join(" ")

    // Measured over every label in both tables, so the label column neither differs between the
    // tables nor reflows as rows hide between selections.
    readonly property real labelColumnWidth: {
        let widest = 0;
        for (let i = 0; i < labelMetrics.children.length; ++i)
            widest = Math.max(widest, labelMetrics.children[i].implicitWidth);
        return Math.ceil(widest);
    }

    Column {
        id: labelMetrics
        visible: false

        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Size")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Dimensions")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Modified")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Camera")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Lens")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Aperture")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Exposure")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("ISO")
        }
        HnLabel {
            role: HnTypographyRole.Caption
            rawText: qsTr("Focal length")
        }
    }

    component RowLabel: HnLabel {
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
        horizontalAlignment: Text.AlignLeft
        Layout.preferredWidth: root.labelColumnWidth
        Layout.minimumWidth: root.labelColumnWidth
        Layout.alignment: Qt.AlignTop | Qt.AlignLeft
    }

    // Values wrap and never elide; Text.Wrap also breaks a single over-long word so nothing
    // overflows at the 220 px minimum pane width (REQ-F-019, REQ-NF-002).
    component RowValue: HnLabel {
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textPrimary
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        Layout.alignment: Qt.AlignTop | Qt.AlignLeft
    }

    Flickable {
        id: scrollArea
        objectName: "previewScrollArea"
        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        visible: root.preview.hasEntry
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        readonly property string entryPath: root.controller.currentPath + "/" + root.preview.name
        onEntryPathChanged: contentY = 0

        ScrollBar.vertical: ScrollBar {
            parent: root
            anchors.right: parent.right
            anchors.top: scrollArea.top
            anchors.bottom: scrollArea.bottom
        }

        ColumnLayout {
            id: content
            width: scrollArea.width
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            Item {
                id: imageArea
                objectName: "previewImageArea"
                // Source metadata, fixed per file and independent of the requested decode size; square
                // until known, and always square for entries that are not images.
                readonly property real aspectRatio: root.preview.sourcePixelSize.width > 0 && root.preview.sourcePixelSize.height > 0 ? root.preview.sourcePixelSize.height / root.preview.sourcePixelSize.width : 1
                readonly property real frameHeight: Math.min(240, Math.round(width * aspectRatio))

                Layout.fillWidth: true
                Layout.preferredHeight: frameHeight
                // Reserved for every entry: a thumbnail when one exists, otherwise the listing's icon
                // (SPEC.md REQ-F-012/013).
                visible: root.preview.hasEntry
                onWidthChanged: root.reportImageAreaSize()

                readonly property int iconExtent: Math.min(128, Math.floor(width))
                // Re-read whenever the selected chain changes; a chain the listing already failed is not
                // requested again (IconFallbacks).
                readonly property bool knownUnresolved: IconFallbacks.isUnresolved(root.preview.iconName)
                // Pinned on failure so a resize (new sourceSize) does not re-request a known miss.
                property string failedChain
                readonly property bool skipRequest: knownUnresolved || failedChain === root.preview.iconName
                readonly property bool showFallback: skipRequest || previewThemeIcon.hasError
                // Directory chains, and only they, begin with "folder" (IconNameResolver), so the one
                // exposed iconName is enough to pick the fallback glyph.
                readonly property bool isFolderIconName: root.preview.iconName === "folder" || root.preview.iconName.startsWith("folder/")

                PreviewImageItem {
                    anchors.fill: parent
                    image: root.preview.image
                    visible: root.preview.hasImage
                    radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height, HnAppearance.revision)
                }
                HnIcon {
                    id: previewThemeIcon
                    objectName: "previewThemeIcon"
                    anchors.centerIn: parent
                    size: imageArea.iconExtent
                    source: !root.preview.hasEntry || root.preview.hasImage || imageArea.skipRequest ? "" : "image://icon/" + root.preview.iconName
                    visible: !root.preview.hasImage && !imageArea.showFallback
                    onHasErrorChanged: if (hasError) {
                        const chain = root.preview.iconName;
                        IconFallbacks.markUnresolved(chain);
                        // Deferred: the failure is reported from inside the source assignment itself.
                        Qt.callLater(() => imageArea.failedChain = chain);
                    }
                }
                HnIcon {
                    id: previewFallbackIcon
                    objectName: "previewFallbackIcon"
                    anchors.centerIn: parent
                    size: imageArea.iconExtent
                    source: root.preview.hasImage || !imageArea.showFallback ? "" : imageArea.isFolderIconName ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg" : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
                    visible: !root.preview.hasImage && imageArea.showFallback
                }
                Rectangle {
                    objectName: "previewIconFailurePlaceholder"
                    anchors.centerIn: parent
                    width: imageArea.iconExtent
                    height: imageArea.iconExtent
                    radius: 8
                    color: "transparent"
                    border.color: HoloniightPalette.textMuted
                    visible: !root.preview.hasImage && imageArea.showFallback && previewFallbackIcon.hasError
                    Text {
                        anchors.centerIn: parent
                        text: "?"
                        color: HoloniightPalette.textMuted
                        font.pixelSize: Math.min(64, imageArea.iconExtent * 0.6)
                    }
                }
            }

            HnLabel {
                objectName: "previewFileName"
                role: HnTypographyRole.Subheading
                font.bold: true
                color: HoloniightPalette.textPrimary
                rawText: root.preview.name
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            HnLabel {
                objectName: "previewTypeDescription"
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: root.preview.mimeTypeDescription
                visible: text.length > 0
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.topMargin: -HnMetrics.internalSpacing(HnControlSize.Normal) / 2
            }

            GridLayout {
                id: metadataTable
                objectName: "previewMetadataTable"
                Layout.fillWidth: true
                columns: 2
                columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
                rowSpacing: 2

                RowLabel {
                    rawText: qsTr("Size")
                    visible: root.sizeText.length > 0
                }
                RowValue {
                    objectName: "previewSizeValue"
                    rawText: root.sizeText
                    visible: root.sizeText.length > 0
                }
                RowLabel {
                    rawText: qsTr("Dimensions")
                    visible: root.dimensionsText.length > 0
                }
                RowValue {
                    objectName: "previewDimensionsValue"
                    rawText: root.dimensionsText
                    visible: root.dimensionsText.length > 0
                }
                RowLabel {
                    rawText: qsTr("Modified")
                    visible: root.modifiedText.length > 0
                }
                RowValue {
                    objectName: "previewModifiedValue"
                    rawText: root.modifiedText
                    visible: root.modifiedText.length > 0
                }
            }

            // The separator belongs to the EXIF section and hides with it, so non-EXIF entries do not
            // end in a dangling rule (DESIGN.md conflict item 2).
            HnSeparator {
                Layout.fillWidth: true
                color: HoloniightPalette.borderPassive
                visible: root.preview.exifPresent
            }

            HnLabel {
                objectName: "previewExifHeader"
                role: HnTypographyRole.Body
                font.bold: true
                color: HoloniightPalette.textPrimary
                rawText: qsTr("EXIF")
                visible: root.preview.exifPresent
                Layout.fillWidth: true
            }

            GridLayout {
                id: exifTable
                objectName: "previewExifTable"
                Layout.fillWidth: true
                columns: 2
                columnSpacing: metadataTable.columnSpacing
                rowSpacing: metadataTable.rowSpacing
                visible: root.preview.exifPresent

                RowLabel {
                    rawText: qsTr("Camera")
                    visible: root.cameraText.length > 0
                }
                RowValue {
                    objectName: "previewCameraValue"
                    rawText: root.cameraText
                    visible: root.cameraText.length > 0
                }
                RowLabel {
                    rawText: qsTr("Lens")
                    visible: root.preview.exifLensModel.length > 0
                }
                RowValue {
                    objectName: "previewLensValue"
                    rawText: root.preview.exifLensModel
                    visible: root.preview.exifLensModel.length > 0
                }
                RowLabel {
                    rawText: qsTr("Aperture")
                    visible: root.preview.exifAperture.length > 0
                }
                RowValue {
                    objectName: "previewApertureValue"
                    rawText: root.preview.exifAperture
                    visible: root.preview.exifAperture.length > 0
                }
                RowLabel {
                    rawText: qsTr("Exposure")
                    visible: root.preview.exifExposureTime.length > 0
                }
                RowValue {
                    objectName: "previewExposureValue"
                    rawText: root.preview.exifExposureTime
                    visible: root.preview.exifExposureTime.length > 0
                }
                RowLabel {
                    rawText: qsTr("ISO")
                    visible: root.preview.exifIso.length > 0
                }
                RowValue {
                    objectName: "previewIsoValue"
                    rawText: root.preview.exifIso
                    visible: root.preview.exifIso.length > 0
                }
                RowLabel {
                    rawText: qsTr("Focal length")
                    visible: root.preview.exifFocalLength.length > 0
                }
                RowValue {
                    objectName: "previewFocalLengthValue"
                    rawText: root.preview.exifFocalLength
                    visible: root.preview.exifFocalLength.length > 0
                }
            }

            HnLabel {
                objectName: "previewLoadingNotice"
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("Loading…")
                visible: root.preview.busy
                Layout.fillWidth: true
            }

            HnLabel {
                objectName: "previewErrorNotice"
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textAccent
                rawText: root.preview.previewErrorMessage
                visible: root.preview.previewErrorKind !== PreviewService.None
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
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
