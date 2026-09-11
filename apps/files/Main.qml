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
        enabled: !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !window.controller.tasks.hasPrompt
        onActivated: window.leaveFullscreen()
    }
    Shortcut {
        sequence: "Q"
        enabled: !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.close()
    }
    Shortcut {
        // REQ-F-030/REQ-C-009: fires regardless of which mode owns keyboard focus — including
        // while SEARCH's own TextField holds it — which handleKey()'s per-character dispatch
        // chain can't guarantee without special-casing every mode (see DESIGN.md Interfaces).
        sequence: "Ctrl+C"
        onActivated: window.controller.tasks.cancelCurrentTask()
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
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("yy/dd  copy/cut   p  paste   D  trash")
            }
        }
    }

    QuickLookOverlay {
        objectName: "quickLookOverlay"
        controller: window.controller
    }
}
