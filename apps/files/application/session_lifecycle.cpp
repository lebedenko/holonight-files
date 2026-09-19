#include "session_lifecycle.h"

#include "directory_controller.h"
#include "settings/xdg_paths.h"

void SessionLifecycle::saveState() {
  if (state_saved_ || !restore_enabled_ || !last_location_tracker_.hasCandidate()) {
    return;
  }
  state_saved_ = true;
  // Synchronous: a tiny local write, and the candidate is known Local (DESIGN.md §10 item 3).
  state_store_.save(last_location_tracker_.candidate(), *warnings_);
}
SessionLifecycle::SessionLifecycle(DirectoryController& controller)
    : controller_(controller), state_store_(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()) {}

void SessionLifecycle::shutdown() {
  if (started_) {
    return;
  }
  started_ = true;
  controller_.navigation_.restore_candidate_.clear();
  pending_workers_ = {&controller_.navigation_.model_, &controller_.preview_, &controller_.tasks_};
  controller_.navigation_.model_.shutdown();
  controller_.preview_.shutdown();
  controller_.tasks_.shutdown();
}

void SessionLifecycle::workerFinished(const QObject* worker) {
  if (!started_ || finished_ || !pending_workers_.remove(worker) || !pending_workers_.isEmpty()) {
    return;
  }
  finished_ = true;
  // Classification deliveries precede the directory worker's shutdown notification.
  saveState();
  emit controller_.shutdownFinished();
}
