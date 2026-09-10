.pragma library

function normalized(event) {
    switch (event.key) {
    case Qt.Key_Space: return " ";
    case Qt.Key_Escape: return "Escape";
    case Qt.Key_Return: return "Return";
    case Qt.Key_Enter: return "Enter";
    default: return event.text;
    }
}

function press(event, controller, popup) {
    const key = normalized(event);
    if (popup && key !== " " && key !== "Escape" && key !== "j" && key !== "k")
        return;
    if (key === " " && event.isAutoRepeat) {
        event.accepted = true;
        return;
    }
    if (key.length > 0 && controller.handleKey(key))
        event.accepted = true;
}

function release(event) {
    if (event.key === Qt.Key_Space)
        event.accepted = true;
}

function overrideShortcut(event, popup, blockEscape) {
    // blockEscape: true while VISUAL mode is active (Escape there must exit VISUAL, not leave
    // fullscreen) — INSERT/SEARCH move focus onto their own text field, which claims Escape
    // itself, so they never need this parameter.
    if (event.key === Qt.Key_Space || (event.key === Qt.Key_Escape && (popup || blockEscape)))
        event.accepted = true;
}
