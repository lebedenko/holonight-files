#include "preview_selection.h"

#include "directory_controller.h"

#include <sys/stat.h>

void PreviewSelection::syncPreviewTarget() {
  if (controller_.navigation_.cursor_row_ < 0 ||
      controller_.navigation_.cursor_row_ >= controller_.navigation_.proxy_.rowCount()) {
    if (quick_look_open_) {
      quick_look_open_ = false;
      controller_.preview_.setQuickLookActive(false);
      emit controller_.changed();
    }
    if (!preview_target_path_.isEmpty()) {
      preview_target_path_.clear();
      controller_.preview_.clear();
    }
    return;
  }
  const auto sourceIndex = controller_.navigation_.proxy_.mapToSource(
      controller_.navigation_.proxy_.index(controller_.navigation_.cursor_row_, 0));
  const auto path = controller_.navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
  if (quick_look_open_ && path != preview_target_path_) {
    // Quick Look is pinned to one file. Keys can no longer move the cursor while it is open, so a different
    // entry under the cursor means the listing shifted externally; close rather than retarget the overlay.
    quick_look_open_ = false;
    controller_.preview_.setQuickLookActive(false);
    emit controller_.changed();
  }
  if (path != preview_target_path_) {
    const auto files = controller_.navigation_.watcher_.files();
    if (!files.isEmpty()) {
      controller_.navigation_.watcher_.removePaths(files);
    }
  }
  const auto mode = controller_.navigation_.model_.data(sourceIndex, DirectoryModel::ModeRole).toUInt();
  if (S_ISREG(mode) && !controller_.navigation_.watcher_.files().contains(path)) {
    controller_.navigation_.watcher_.addPath(path);
    emit controller_.navigated();
  }
  preview_target_path_ = path;
  controller_.preview_.setTarget(
      path, controller_.navigation_.model_.data(sourceIndex, DirectoryModel::IsDirRole).toBool(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::SizeRole).toLongLong(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::ModifiedRole).toDateTime(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::ModeRole).toUInt(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::StatFailedRole).toBool(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::StatErrorRole).toString(),
      controller_.navigation_.model_.data(sourceIndex, DirectoryModel::IconNameRole).toString(), preview_revision_);
}
bool PreviewSelection::canPreviewSelection() const {
  return controller_.navigation_.cursor_row_ >= 0 &&
         controller_.navigation_.cursor_row_ < controller_.navigation_.proxy_.rowCount() &&
         controller_.preview_.hasEntry() && controller_.preview_.quickLookEligible() &&
         !controller_.navigation_.model_
              .data(controller_.navigation_.proxy_.mapToSource(
                        controller_.navigation_.proxy_.index(controller_.navigation_.cursor_row_, 0)),
                    DirectoryModel::StatFailedRole)
              .toBool();
}
