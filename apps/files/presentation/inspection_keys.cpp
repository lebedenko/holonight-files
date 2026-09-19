#include "inspection_keys.h"

namespace {
QString normalized(int key, const QString& text) {
  switch (key) {
    case Qt::Key_Space:
      return QStringLiteral(" ");
    case Qt::Key_Escape:
      return QStringLiteral("Escape");
    case Qt::Key_Return:
      return QStringLiteral("Return");
    case Qt::Key_Enter:
      return QStringLiteral("Enter");
    default:
      return text;
  }
}
}  // namespace

// QML invokes this method through the engine-owned singleton instance.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool InspectionKeys::press(int key, const QString& text, int modifiers, bool autoRepeat,
                           DirectoryController* controller, bool popup) const {
  if (popup && (modifiers & Qt::ControlModifier) != 0 && (key == Qt::Key_O || key == Qt::Key_I)) {
    if (controller != nullptr) {
      if (key == Qt::Key_O) {
        controller->navigateHistoryBack();
      } else {
        controller->navigateHistoryForward();
      }
    }
    return true;
  }
  const auto input = normalized(key, text);
  if (popup && input != " " && input != "Escape" && input != "j" && input != "k") {
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
