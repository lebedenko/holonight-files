pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Controls.Popup {
    id: root
    objectName: "pathFinderPopup"
    required property DirectoryController controller
    property bool directoriesOnly: false
    property string currentFolderRoot: ""
    property int selectedRow: 0

    function start(directories: bool): void {
        directoriesOnly = directories;
        currentFolderRoot = controller.currentPath;
        controller.finder.start(controller.finder.homePath, directories);
        selectedRow = 0;
        searchField.text = "";
        open();
    }
    function accept(): void {
        if (controller.acceptFinderResult(selectedRow))
            close();
    }
    function highlightedPath(path: string, positions: var): string {
        let result = "";
        for (let i = 0; i < path.length; ++i) {
            let character = path[i];
            if (character === "&")
                character = "&amp;";
            else if (character === "<")
                character = "&lt;";
            else if (character === ">")
                character = "&gt;";
            result += positions.indexOf(i) >= 0 ? "<b>" + character + "</b>" : character;
        }
        return result;
    }

    parent: Controls.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 4) : 0
    width: parent ? Math.min(680, parent.width - 32) : 680
    height: parent ? Math.min(470, parent.height - 32) : 470
    padding: HnMetrics.internalSpacing(HnControlSize.Normal)
    modal: true
    focus: true
    closePolicy: Controls.Popup.NoAutoClose
    onOpened: searchField.forceActiveFocus()
    onClosed: controller.finder.stop()

    Controls.Overlay.modal: Rectangle {
        color: HoloniightPalette.scrim
    }
    background: Rectangle {
        color: HoloniightPalette.surface
        border.color: HoloniightPalette.borderPassive
        border.width: HnMetrics.borderWidth
        radius: HnAppearance.roundedRadius(HnSurfaceRole.Control, width, height, HnAppearance.revision)
    }

    contentItem: ColumnLayout {
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)
        HnLabel {
            Layout.fillWidth: true
            role: HnTypographyRole.Body
            rawText: root.directoriesOnly ? qsTr("Jump to Directory") : qsTr("Find File")
        }
        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: qsTr("Home")
                highlighted: root.controller.finder.rootPath === root.controller.finder.homePath
                onClicked: {
                    root.controller.finder.setRoot(root.controller.finder.homePath);
                    root.selectedRow = 0;
                    searchField.forceActiveFocus();
                }
            }
            Controls.Button {
                text: qsTr("Current Folder")
                highlighted: root.controller.finder.rootPath === root.currentFolderRoot
                enabled: root.currentFolderRoot.length > 0
                onClicked: {
                    root.controller.finder.setRoot(root.currentFolderRoot);
                    root.selectedRow = 0;
                    searchField.forceActiveFocus();
                }
            }
            Controls.CheckBox {
                text: qsTr("Include hidden")
                checked: root.controller.finder.includeHidden
                onToggled: {
                    root.controller.finder.setIncludeHidden(checked);
                    root.selectedRow = 0;
                    searchField.forceActiveFocus();
                }
            }
            HnLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                rawText: root.controller.finder.rootPath
            }
        }
        Controls.TextField {
            id: searchField
            objectName: "pathFinderSearchField"
            Layout.fillWidth: true
            placeholderText: root.directoriesOnly ? qsTr("Directory name or path") : qsTr("File name or path")
            onTextChanged: {
                root.controller.finder.setQuery(text);
                root.selectedRow = 0;
            }
            Keys.onDownPressed: root.selectedRow = Math.min(root.selectedRow + 1, Math.max(0, results.count - 1))
            Keys.onUpPressed: root.selectedRow = Math.max(root.selectedRow - 1, 0)
            Keys.onReturnPressed: root.accept()
            Keys.onEnterPressed: root.accept()
            Keys.onEscapePressed: root.close()
        }
        HnLabel {
            Layout.fillWidth: true
            visible: root.controller.finder.error.length > 0 || root.controller.finder.scanning
            rawText: root.controller.finder.error.length > 0 ? root.controller.finder.error : qsTr("Scanning… %1 paths").arg(root.controller.finder.indexedCount)
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: results
                objectName: "pathFinderResults"
                clip: true
                model: root.controller.finder
                currentIndex: root.selectedRow
                delegate: Controls.ItemDelegate {
                    id: resultRow
                    required property int index
                    required property string relativePath
                    required property var positions
                    width: ListView.view.width
                    highlighted: index === root.selectedRow
                    onClicked: {
                        root.selectedRow = index;
                        root.accept();
                    }
                    contentItem: Controls.Label {
                        textFormat: Text.RichText
                        elide: Text.ElideMiddle
                        text: root.highlightedPath(resultRow.relativePath, resultRow.positions)
                    }
                }
            }
        }
    }
}
