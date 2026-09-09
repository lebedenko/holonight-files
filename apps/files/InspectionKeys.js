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

function overrideShortcut(event, popup) {
    if (event.key === Qt.Key_Space || (popup && event.key === Qt.Key_Escape))
        event.accepted = true;
}
