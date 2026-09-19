#pragma once

#include <QString>

class DirectoryController;

class PreviewSelection {
 public:
  explicit PreviewSelection(DirectoryController& controller) : controller_(controller) {}
  void syncPreviewTarget();
  bool canPreviewSelection() const;

 private:
  friend class DirectoryController;
  friend class NavigationSession;
  friend struct DirectoryControllerTestAccess;
  DirectoryController& controller_;
  QString preview_target_path_;
  quint64 preview_revision_ = 0;
  bool quick_look_open_ = false;
};
