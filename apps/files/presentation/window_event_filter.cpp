#include "window_event_filter.h"

#include "directory_controller.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
// Intercept at the window boundary, before Qt dispatches to editors or application shortcuts.
// Keeping the existing focus item avoids changing editor cursor/selection and input mode.
bool WindowEventFilter::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) {
    return false;
  }
  auto* window = qobject_cast<QQuickWindow*>(watched);
  if (auto* item = qobject_cast<QQuickItem*>(watched)) {
    window = item->window();
  }
  if ((window == nullptr) || window->property("controller").value<DirectoryController*>() != &controller_) {
    return false;
  }
  // The event type is checked above, as required by Qt event filters.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const bool cancel = keyEvent->key() == Qt::Key_C && keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
                      (controller_.tasks()->busy() || controller_.tasks()->hasPrompt());
  if (!controller_.tasks()->hasPrompt() && !cancel) {
    return false;
  }
  event->accept();
  if (event->type() == QEvent::ShortcutOverride) {
    return true;
  }
  if (cancel) {
    controller_.tasks()->cancelCurrentTask();
  } else {
    QString key = keyEvent->text();
    if (keyEvent->key() == Qt::Key_Escape) {
      key = QStringLiteral("Escape");
    }
    // Modifier presses do not answer a prompt before their printable key arrives.
    if (keyEvent->key() != Qt::Key_Shift && keyEvent->key() != Qt::Key_Control && keyEvent->key() != Qt::Key_Alt &&
        keyEvent->key() != Qt::Key_Meta) {
      controller_.handleKey(key);
    }
  }
  return true;
}
