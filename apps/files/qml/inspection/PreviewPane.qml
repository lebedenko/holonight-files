pragma ComponentBehavior: Bound
import QtQuick
import Holonight.FileBrowser.Preview as BrowserPreview

BrowserPreview.FilePreview {
    required property DirectoryController controller
    service: controller.preview
    folderFallback: "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg"
    fileFallback: "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
}
