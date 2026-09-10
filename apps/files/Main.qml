pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
// Initialize the configured style before shared controls import Basic.
// qmllint disable unused-imports
import QtQuick.Controls
// qmllint enable unused-imports
import Holonight.Core
import Holonight.Controls

HnApplicationWindow {
    id: window
    objectName: "filesWindow"
    required property DirectoryController controller
    width: 1000
    height: 700
    minimumWidth: 420
    minimumHeight: 280
    visible: true
    title: qsTr("HoloNight Files")

    function toggleFullscreen(): void {
        WindowState.setFullscreen(window, window.visibility !== Window.FullScreen);
    }

    function leaveFullscreen(): void {
        if (window.visibility === Window.FullScreen)
            WindowState.setFullscreen(window, false);
    }

    Shortcut {
        sequence: "F"
        enabled: window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: window.leaveFullscreen()
    }
    Shortcut {
        sequence: "Q"
        enabled: window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        ModeStatusBar {
            objectName: "modeStatusBar"
            controller: window.controller
            Layout.fillWidth: true
            Layout.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            PlacesPanel {
                objectName: "placesPanel"
                controller: window.controller
                Layout.preferredWidth: 200
                Layout.fillHeight: true
            }

            SplitView {
                objectName: "listingPreviewSplit"
                orientation: Qt.Horizontal
                Layout.fillWidth: true
                Layout.fillHeight: true

                DirectoryListing {
                    objectName: "directoryListing"
                    controller: window.controller
                    SplitView.fillWidth: true
                }

                PreviewPane {
                    objectName: "previewPane"
                    controller: window.controller
                    SplitView.preferredWidth: 320
                    SplitView.minimumWidth: 220
                }
            }
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 6
            spacing: 12
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr(".  hidden   s  reverse sort   F  fullscreen")
            }
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("Space  quick look   Q  quit")
            }
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("i/a  rename   o/O  create   v  select   /  search")
            }
        }
    }

    QuickLookOverlay {
        objectName: "quickLookOverlay"
        controller: window.controller
    }
}
