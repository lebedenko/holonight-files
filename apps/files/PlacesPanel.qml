pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Controls

ListView {
    id: root

    required property DirectoryController controller

    clip: true

    model: root.controller ? root.controller.places : null

    delegate: HnListDelegate {
        id: delegate

        required property int index
        required property string name
        required property string path

        objectName: "placeDelegate"
        width: root.width
        title: name
        highlighted: root.controller && root.controller.currentPath === path

        onClicked: root.controller.open(delegate.path)
    }
}
