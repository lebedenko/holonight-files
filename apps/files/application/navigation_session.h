#pragma once

#include "directory_model.h"
#include "directory_proxy_model.h"
#include "jump_list.h"

#include <QFileSystemWatcher>
#include <QHash>

class DirectoryController;

class NavigationSession {
 public:
  explicit NavigationSession(DirectoryController& controller) : controller_(controller) {}
  void openInternal(const QString& requestedPath, const QString& fallbackReason, const QString& restoreName,
                    bool recordHistory);
  QString outgoingCursorName() const;
  void traverseHistory(int direction, int count);
  void cancelPendingRestore();
  void maybeApplyPendingRestore();
  void navigateInto(int proxyRow);
  void navigateParent();
  void openEntry(int proxyRow);
  void setCursorRow(qint64 row);
  void clampCursorRow();
  QString entryNameAt(int proxyRow) const;
  void activateBookmark(int placesRow);
  void handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available);
  void openRestoreCandidate(const QString& path);
  void handleRestoreValidated(const QString& path, RestoreOutcome outcome);

 private:
  friend class DirectoryController;
  friend class EditingSession;
  friend class PreviewSelection;
  friend class SessionLifecycle;
  friend struct DirectoryControllerTestAccess;
  DirectoryController& controller_;
  DirectoryModel model_;
  DirectoryProxyModel proxy_;
  QFileSystemWatcher watcher_;
  QString current_path_;
  JumpList jump_list_;
  quint64 navigation_serial_ = 0;
  quint64 restore_serial_ = 0;
  QString restore_candidate_;
  QString pending_restore_name_;
  QHash<quint64, quint64> bookmark_dispatch_navigation_serial_;
  bool awaiting_initial_load_ = false;
  int cursor_row_ = 0;
};
