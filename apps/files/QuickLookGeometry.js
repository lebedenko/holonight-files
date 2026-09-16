.pragma library

// Pure layout math for QuickLookOverlay.qml (quick-look-redesign DESIGN.md §3.1). Plain numbers and
// strings only: a .pragma library file cannot reference QML items or the PreviewService enum.

// Entry kind per SPEC.md "Entry kind". `hasError` is `previewErrorKind !== PreviewService.None`,
// folded in by the caller.
function classify(hasEntry, busy, mimeType, hasText, hasError) {
    if (!hasEntry)
        return "none";
    if (hasError)
        return "compact";
    // Directories and special files get their mimeType synchronously; a regular file's arrives with
    // its first worker result. setTarget() emits once with busy still false before dispatching, so an
    // empty mimeType alone marks the entry as pending.
    if (mimeType.length === 0)
        return "pending";
    if (mimeType.indexOf("image/") === 0)
        return "image";
    if (hasText)
        return "text";
    // A known non-image type whose decode is still running may yet turn out to be text.
    return busy ? "pending" : "compact";
}

// Largest size with the source aspect ratio that fits inside the bounds; may upscale small sources,
// PreviewImageItem then draws the decoded image aspect-fit inside it.
function fitRect(sourceWidth, sourceHeight, boundsWidth, boundsHeight) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || boundsWidth <= 0 || boundsHeight <= 0)
        return {
            width: 0,
            height: 0
        };
    const scale = Math.min(boundsWidth / sourceWidth, boundsHeight / sourceHeight);
    return {
        width: sourceWidth * scale,
        height: sourceHeight * scale
    };
}

// Never derived from the filename or metadata text: those elide to this width instead of growing
// the card (DESIGN.md §4.1).
function compactCardWidth(minWidth, hintImplicitWidth, iconExtent, horizontalPadding, boundsWidth) {
    const contentWidth = Math.max(minWidth, hintImplicitWidth + 2 * horizontalPadding, iconExtent + 2 * horizontalPadding);
    return Math.min(contentWidth, boundsWidth);
}
