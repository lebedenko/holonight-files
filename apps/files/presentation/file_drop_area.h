#pragma once
#include "directory_controller.h"

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class FileDropArea : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(DirectoryController* controller MEMBER controller_)
  Q_PROPERTY(int targetRow MEMBER target_row_ NOTIFY feedbackChanged)
  Q_PROPERTY(QPointF position READ position NOTIFY feedbackChanged)
  Q_PROPERTY(bool hovering READ hovering NOTIFY feedbackChanged)
  Q_PROPERTY(int action READ action NOTIFY feedbackChanged)
 public:
  explicit FileDropArea(QQuickItem* parent = nullptr);
  [[nodiscard]] QPointF position() const { return position_; }
  [[nodiscard]] bool hovering() const { return hovering_; }
  [[nodiscard]] int action() const { return action_; }
 signals:
  void feedbackChanged();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void update(QDropEvent* event);
  void clear();
  DirectoryController* controller_ = nullptr;
  int target_row_ = -1;
  QPointF position_;
  bool hovering_ = false;
  int action_ = Qt::IgnoreAction;
};
