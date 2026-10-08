#include "inspection_keys.h"

namespace {
QString normalized(int key, const QString& text, bool popup) {
  switch (key) {
    case Qt::Key_Space:
      return QStringLiteral(" ");
    case Qt::Key_Escape:
      return QStringLiteral("Escape");
    case Qt::Key_Return:
      return QStringLiteral("Return");
    case Qt::Key_Enter:
      return QStringLiteral("Enter");
    case Qt::Key_Up:
      return popup ? QStringLiteral("ArrowUp") : text;
    case Qt::Key_Down:
      return popup ? QStringLiteral("ArrowDown") : text;
    default:
      return text;
  }
}
}  // namespace

// QML invokes this method through the engine-owned singleton instance.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool InspectionKeys::press(int key, const QString& text, int modifiers, bool autoRepeat,
                           DirectoryController* controller, bool popup) const {
  const bool control = (modifiers & Qt::ControlModifier) != 0;
  if (popup && control && (key == Qt::Key_O || key == Qt::Key_I)) {
    // History traversal does nothing in VISUAL, matching the window-level shortcuts.
    if (controller != nullptr && controller->vim()->currentMode() != VimModeController::Mode::Visual) {
      if (key == Qt::Key_O) {
        controller->navigateHistoryBack();
      } else {
        controller->navigateHistoryForward();
      }
    }
    return true;
  }
  if (!popup && control && (key == Qt::Key_U || key == Qt::Key_D)) {
    return controller != nullptr &&
           controller->handleKey(key == Qt::Key_U ? QStringLiteral("Ctrl+U") : QStringLiteral("Ctrl+D"));
  }
  const auto input = normalized(key, text, popup);
  if (popup && input != " " && input != "Escape" && input != "j" && input != "k" && input != "ArrowUp" &&
      input != "ArrowDown") {
    return false;
  }
  if (input == " " && autoRepeat) {
    return true;
  }
  return !input.isEmpty() && controller != nullptr && controller->handleKey(input);
}

// QML invokes this method through the engine-owned singleton instance.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool InspectionKeys::release(int key) const { return key == Qt::Key_Space; }

// QML invokes this method through the engine-owned singleton instance.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool InspectionKeys::overrideShortcut(int key, bool popup, bool blockEscape) const {
  return key == Qt::Key_Space || (key == Qt::Key_Escape && (popup || blockEscape));
}
