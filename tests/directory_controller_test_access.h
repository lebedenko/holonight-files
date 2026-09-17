#pragma once

#include "directory_controller.h"
#include "directory_model_test_access.h"

#include <functional>
#include <memory>
#include <utility>

struct DirectoryControllerTestAccess {
  static DirectoryModel& model(DirectoryController& controller) { return controller.model_; }
  static const JumpList& jumpList(const DirectoryController& controller) { return controller.jump_list_; }
  static void beforeCommit(DirectoryController& controller,
                           std::function<void(const VimModeController::InsertCommitResult&)> callback) {
    controller.before_commit_for_test_ = std::move(callback);
  }
  static void setWarningSink(DirectoryController& controller, std::shared_ptr<WarningSink> sink) {
    controller.warnings_ = std::move(sink);
  }
  static void setLocationClassifier(DirectoryController& controller,
                                    std::shared_ptr<const LocationClassifier> classifier) {
    DirectoryModelTestAccess::setLocationClassifier(controller.model_, std::move(classifier));
  }
};
