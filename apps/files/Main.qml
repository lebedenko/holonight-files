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
    width: 1000
    height: 700
    minimumWidth: 420
    minimumHeight: 280
    visible: true
    title: qsTr("HoloNight Files")

    property bool restoreMaximized: false

    function leaveFullscreen(): void {
        if (window.visibility === Window.FullScreen) {
            if (window.restoreMaximized)
                window.showMaximized();
            else
                window.showNormal();
        }
    }

    Shortcut {
        sequence: "F"
        onActivated: {
            if (window.visibility === Window.FullScreen) {
                window.leaveFullscreen();
            } else {
                window.restoreMaximized = window.visibility === Window.Maximized;
                window.showFullScreen();
            }
        }
    }
    Shortcut {
        sequence: "Escape"
        onActivated: window.leaveFullscreen()
    }
    Shortcut {
        sequence: "Q"
        onActivated: window.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            HnEmptyState {
                id: emptyState
                objectName: "emptyState"
                anchors.centerIn: parent
                titleText: qsTr("No folder open")
            }
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 6
            spacing: 12
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("F  fullscreen")
            }
            HnLabel {
                role: HnTypographyRole.Caption
                color: HoloniightPalette.textMuted
                rawText: qsTr("Q  quit")
            }
        }
    }
}
