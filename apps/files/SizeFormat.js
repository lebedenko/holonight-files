.pragma library

// The single size formatter shared by DirectoryListing.qml and PreviewPane.qml, so the listing and
// the sidebar can never render different size strings for the same file. Divides by 1024 but
// labels with KB/MB/GB/TB, matching the mockup's "8.3 MB". Negative input (no size) returns "".
function formatSize(bytes) {
    if (bytes < 0)
        return "";
    if (bytes < 1024)
        return qsTr("%1 B").arg(bytes);
    const units = ["KB", "MB", "GB", "TB"];
    let value = bytes / 1024;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex += 1;
    }
    return qsTr("%1 %2").arg(value.toFixed(1)).arg(units[unitIndex]);
}
