#pragma once

#include "directory_model.h"

// Internal synchronization/failure seam, configured before starting a walk and copied into it.
struct DirectoryModelTestAccess {
  static void beforeOpen(DirectoryModel& model, std::function<void()> callback) {
    model.before_open_for_test_ = std::move(callback);
  }
  static void failReadAfter(DirectoryModel& model, int entries) { model.read_error_after_for_test_ = entries; }
};
