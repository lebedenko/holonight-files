#pragma once

#include "state/last_location_tracker.h"
#include "state/state_store.h"
#include "warning_sink.h"

#include <QSet>

#include <memory>

class DirectoryController;

class SessionLifecycle {
 public:
  explicit SessionLifecycle(DirectoryController& controller);
  void saveState();
  void shutdown();
  void workerFinished(const QObject* worker);

 private:
  friend class DirectoryController;
  friend struct DirectoryControllerTestAccess;
  DirectoryController& controller_;
  LastLocationTracker last_location_tracker_;
  StateStore state_store_;
  std::shared_ptr<WarningSink> warnings_ = std::make_shared<StderrWarningSink>();
  bool restore_enabled_ = false;
  bool state_saved_ = false;
  QSet<const QObject*> pending_workers_;
  bool started_ = false;
  bool finished_ = false;
};
