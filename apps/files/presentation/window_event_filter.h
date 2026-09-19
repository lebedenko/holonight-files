#pragma once
#include <QObject>
class DirectoryController;
class WindowEventFilter : public QObject {
 public:
  explicit WindowEventFilter(DirectoryController& controller) : controller_(controller) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  DirectoryController& controller_;
};
