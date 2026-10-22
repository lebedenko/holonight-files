pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property DirectoryController controller
    function focusListing(): void {
        const listItem = presentation.item as ListPresentation;
        const iconItem = presentation.item as IconPresentation;
        if (listItem)
            listItem.focusListing();
        else if (iconItem)
            iconItem.focusListing();
    }
    Loader {
        id: presentation
        anchors.fill: parent
        sourceComponent: root.controller.iconView ? icons : list
    }
    Component {
        id: list
        ListPresentation {
            controller: root.controller
        }
    }
    Component {
        id: icons
        IconPresentation {
            controller: root.controller
        }
    }
}
