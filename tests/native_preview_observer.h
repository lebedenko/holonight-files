#pragma once

#include "directory_controller.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <atomic>
#include <memory>

// Compiled only into the non-installed lab. Never changes application state or input.
class NativePreviewObserver : public QObject {
 public:
  NativePreviewObserver(DirectoryController& controller, QElapsedTimer clock);
  void attach(QObject* root);

 private:
  void snapshot(const QString& reason);
  void write(QJsonObject event);
  DirectoryController& controller_;
  QElapsedTimer clock_;
  QFile output_;
  QPointer<QQuickWindow> window_;
  QTimer timer_;
  std::shared_ptr<std::atomic_int> attempts_ = std::make_shared<std::atomic_int>(0);
  std::shared_ptr<std::atomic_int> frames_ = std::make_shared<std::atomic_int>(0);
  qint64 last_tick_ = 0;
  qint64 max_gap_ = 0;
  int ticks_ = 0;
  bool ended_ = false;
};
