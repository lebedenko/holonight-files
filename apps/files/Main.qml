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
    property real sidebarWidth: 200
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
        spacing: 0

        AppHeaderBar {
            objectName: "appHeaderBar"
            controller: window.controller
            sidebarWidth: window.sidebarWidth
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            Item {
                id: sidebarContainer
                objectName: "sidebarContainer"
                Layout.preferredWidth: window.sidebarWidth
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: parent
                    color: HoloniightPalette.surface
                }

                PlacesPanel {
                    objectName: "placesPanel"
                    controller: window.controller
                    anchors.fill: parent
                }

                HnSeparator {
                    orientation: Qt.Vertical
                    color: HoloniightPalette.borderPassive
                    anchors {
                        top: parent.top
                        bottom: parent.bottom
                        right: parent.right
                    }
                }
            }

            SplitView {
                objectName: "listingPreviewSplit"
                orientation: Qt.Horizontal
                Layout.fillWidth: true
                Layout.fillHeight: true

                handle: Item {
                    implicitWidth: HnMetrics.internalSpacing(HnControlSize.Compact) + HnMetrics.separatorWidth

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: HnMetrics.separatorWidth
                        height: parent.height
                        color: SplitHandle.pressed ? HoloniightPalette.borderActive : (SplitHandle.hovered ? HoloniightPalette.borderHover : HoloniightPalette.borderPassive)
                    }
                }

                DirectoryListing {
                    objectName: "directoryListing"
                    controller: window.controller
                    SplitView.fillWidth: true
                }

                Item {
                    id: previewContainer
                    objectName: "previewContainer"
                    SplitView.preferredWidth: 320
                    SplitView.minimumWidth: 220

                    Rectangle {
                        anchors.fill: parent
                        color: HoloniightPalette.surface
                    }

                    PreviewPane {
                        objectName: "previewPane"
                        controller: window.controller
                        anchors.fill: parent
                    }
                }
            }
        }

        Rectangle {
            id: footerBar
            objectName: "footerBar"
            Layout.fillWidth: true
            implicitHeight: modeStatusBar.implicitHeight + 2 * HnMetrics.internalSpacing(HnControlSize.Normal)
            color: HoloniightPalette.surface

            HnSeparator {
                color: HoloniightPalette.borderPassive
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }
            }

            ModeStatusBar {
                id: modeStatusBar
                objectName: "modeStatusBar"
                controller: window.controller
                anchors.fill: parent
                anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            }
        }
    }

    QuickLookOverlay {
        objectName: "quickLookOverlay"
        controller: window.controller
    }
}
