pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Holonight.Core
import Holonight.Controls

// Passive view. The consumer owns the cursor, selection, activation and key commands.
// Files replaces the delegate to retain its editing UI and line-number gutter.
ListView {
    id: root
    property var selectedPaths: []
    property string iconProvider: "image://icon/"
    signal cursorRequested(int row)
    signal activationRequested(int row)
    signal selectionToggled(int row)

    clip: true
    Controls.ScrollBar.vertical: Controls.ScrollBar {}
    onCurrentIndexChanged: {
        if (currentIndex < 0)
            return;
        if (currentIndex === 0)
            positionViewAtBeginning();
        else if (currentIndex === count - 1)
            positionViewAtEnd();
        else
            positionViewAtIndex(currentIndex, ListView.Contain);
    }
    delegate: HnListDelegate {
        id: entry
        required property int index
        required property string name
        required property string path
        required property bool isDir
        required property bool isParent
        required property string iconName
        required property bool statFailed
        required property string statError
        width: root.width
        highlighted: ListView.isCurrentItem || root.selectedPaths.indexOf(path) >= 0
        title: name
        subtitle: statFailed ? statError : (isParent ? qsTr("Parent folder") : (isDir ? qsTr("Folder") : ""))
        leadingContent: root.iconProvider.length > 0 ? fileIcon : null
        Component {
            id: fileIcon
            HnIcon {
                size: HnMetrics.controlHeight(HnControlSize.Compact)
                source: root.iconProvider + entry.iconName
                rendering: HnIcon.Original
            }
        }
        onClicked: root.cursorRequested(index)
        onDoubleClicked: root.activationRequested(index)
        onPressAndHold: root.selectionToggled(index)
    }
}
