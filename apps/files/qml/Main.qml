pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
// Initialize the configured style before shared controls import Basic.
// qmllint disable unused-imports
import QtQuick.Controls as Controls
// qmllint enable unused-imports
import Holonight.Core
import Holonight.Controls

HnApplicationWindow {
    id: window
    objectName: "filesWindow"
    required property DirectoryController controller
    property real sidebarWidth: 220
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
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !window.controller.tasks.hasPrompt && !pathFinder.visible
        onActivated: window.leaveFullscreen()
    }
    Shortcut {
        sequence: "Q"
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.close()
    }
    Shortcut {
        // navigation-history REQ-F-020: matched by key code, so Ctrl+I is never mistaken for Tab.
        // Window context reaches it from the listing, a delegate editor or Quick Look alike.
        sequence: "Ctrl+O"
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.controller.navigateHistoryBack()
    }
    Shortcut {
        sequence: "Ctrl+I"
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: window.controller.navigateHistoryForward()
    }
    Shortcut {
        // REQ-F-030/REQ-C-009: fires regardless of which mode owns keyboard focus — including
        // while SEARCH's own TextField holds it — which handleKey()'s per-character dispatch
        // chain can't guarantee without special-casing every mode (see DESIGN.md Interfaces).
        sequence: "Ctrl+C"
        onActivated: window.controller.tasks.cancelCurrentTask()
    }
    Shortcut {
        sequence: "Ctrl+G"
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && !window.controller.quickLookOpen && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: pathFinder.start(true)
    }
    Shortcut {
        sequence: "Ctrl+P"
        enabled: !pathFinder.visible && !window.controller.tasks.hasPrompt && !window.controller.quickLookOpen && window.controller.vim.currentMode === VimModeController.Normal
        onActivated: pathFinder.start(false)
    }

    PathFinderPopup {
        id: pathFinder
        controller: window.controller
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeaderBar {
            // Paint the owned bottom boundary above adjacent content backgrounds.
            z: 1
            objectName: "appHeaderBar"
            controller: window.controller
            sidebarWidth: window.sidebarWidth
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                id: sidebarContainer
                objectName: "sidebarContainer"
                Layout.preferredWidth: window.sidebarWidth
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: parent
                    color: HoloniightPalette.surface
                }

                SidebarPanel {
                    objectName: "sidebarPanel"
                    controller: window.controller
                    anchors.fill: parent
                }

                HnSeparator {
                    objectName: "sidebarDivider"
                    crossAxisAlignment: HnSeparator.Trailing
                    orientation: Qt.Vertical
                    color: HoloniightPalette.borderPassive
                    anchors {
                        top: parent.top
                        bottom: parent.bottom
                        right: parent.right
                    }
                }
            }

            Controls.SplitView {
                objectName: "listingPreviewSplit"
                orientation: Qt.Horizontal
                Layout.fillWidth: true
                Layout.fillHeight: true

                handle: HnSeparator {
                    id: splitHandle
                    objectName: "listingPreviewDivider"
                    orientation: Qt.Vertical
                    color: splitHandle.Controls.SplitHandle.pressed ? HoloniightPalette.borderActive : (splitHandle.Controls.SplitHandle.hovered ? HoloniightPalette.borderHover : HoloniightPalette.borderPassive)

                    containmentMask: Item {
                        x: (splitHandle.width - width) / 2
                        width: HnMetrics.internalSpacing(HnControlSize.Compact) + HnMetrics.separatorWidth
                        height: splitHandle.height
                    }
                }

                DirectoryListing {
                    objectName: "directoryListing"
                    controller: window.controller
                    Controls.SplitView.fillWidth: true
                }

                Item {
                    id: previewContainer
                    objectName: "previewContainer"
                    Controls.SplitView.preferredWidth: 220
                    Controls.SplitView.minimumWidth: 220

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
                objectName: "footerDivider"
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
