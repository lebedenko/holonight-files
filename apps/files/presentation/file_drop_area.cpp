#include "file_drop_area.h"

#include "file_transfer.h"

#include <QDragEnterEvent>
#include <QMimeData>

FileDropArea::FileDropArea(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemAcceptsDrops, true);
  connect(this, &QQuickItem::enabledChanged, this, [this] {
    if (!isEnabled()) {
      clear();
    }
  });
}
void FileDropArea::clear() {
  hovering_ = false;
  action_ = Qt::IgnoreAction;
  target_row_ = -1;
  emit feedbackChanged();
}
void FileDropArea::update(QDropEvent* event) {
  position_ = event->position();
  hovering_ = true;
  action_ = controller_ != nullptr && controller_->transfersEnabled() &&
                    !FileTransfer::localPaths(FileTransfer::mimeUrls(*event->mimeData())).isEmpty()
                ? FileTransfer::action(event->modifiers(), event->possibleActions())
                : Qt::IgnoreAction;
  emit feedbackChanged();
  event->setDropAction(static_cast<Qt::DropAction>(action_));
  // Keep receiving movement for rejection feedback and modifier changes.
  event->accept();
}
void FileDropArea::dragEnterEvent(QDragEnterEvent* event) { update(event); }
void FileDropArea::dragMoveEvent(QDragMoveEvent* event) { update(event); }
void FileDropArea::dragLeaveEvent(QDragLeaveEvent* event) {
  event->accept();
  clear();
}
void FileDropArea::dropEvent(QDropEvent* event) {
  update(event);
  const bool accepted = controller_ != nullptr &&
                        controller_->submitDrop(FileTransfer::mimeUrls(*event->mimeData()), target_row_, action_);
  event->setAccepted(accepted);
  clear();
}
