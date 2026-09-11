#pragma once

#include "clipboard_register.h"
#include "directory_model.h"
#include "directory_proxy_model.h"
#include "places_model.h"
#include "preview_service.h"
#include "task_manager.h"
#include "vim_mode_controller.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The facade QML talks to: owns the DirectoryModel/DirectoryProxyModel/PlacesModel/watcher, the
// VimModeController (NORMAL/VISUAL/SEARCH/INSERT), NORMAL-mode key-chord parsing, and
// navigation/open dispatch.
class DirectoryController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
  Q_PROPERTY(QString currentPath READ currentPath NOTIFY changed)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
  Q_PROPERTY(QString directoryError READ directoryError NOTIFY changed)
  Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
  Q_PROPERTY(int cursorRow READ cursorRow NOTIFY changed)
  Q_PROPERTY(DirectoryProxyModel* listing READ listing CONSTANT)
  Q_PROPERTY(PlacesModel* places READ places CONSTANT)
  Q_PROPERTY(PreviewService* preview READ preview CONSTANT)
  Q_PROPERTY(VimModeController* vim READ vim CONSTANT)
  Q_PROPERTY(TaskManager* tasks READ tasks CONSTANT)
  Q_PROPERTY(bool quickLookOpen READ quickLookOpen NOTIFY changed)
 public:
  explicit DirectoryController(QObject* parent = nullptr);
  QString currentPath() const { return current_path_; }
  QString statusMessage() const { return status_message_; }
  QString directoryError() const { return model_.directoryError(); }
  bool scanning() const { return model_.scanning(); }
  int cursorRow() const { return cursor_row_; }
  DirectoryProxyModel* listing() { return &proxy_; }
  PlacesModel* places() { return &places_; }
  PreviewService* preview() { return &preview_; }
  VimModeController* vim() { return &vim_; }
  TaskManager* tasks() { return &tasks_; }
  bool quickLookOpen() const { return quick_look_open_; }
  Q_INVOKABLE void open(const QString& path, const QString& fallbackReason = {});
  Q_INVOKABLE void navigateInto(int proxyRow);
  Q_INVOKABLE void navigateParent();
  Q_INVOKABLE void openEntry(int proxyRow);
  Q_INVOKABLE void toggleHidden();
  Q_INVOKABLE void toggleSortDirection();
  // Returns true when key was recognized and consumed. key is either the raw event.text (e.g.
  // "j", "5", "G") or a synthetic name QML supplies for non-printable keys ("Return"). Returns
  // false while INSERT/SEARCH hold keyboard focus on their own text field (REQ-C-001) — QML never
  // routes those fields' key events through here in the first place, but handleKey() still needs
  // to be a safe no-op if it were called (e.g. from a stale connection).
  Q_INVOKABLE bool handleKey(const QString& key);
  // INSERT — bound to the inline editor's text field (DirectoryListing.qml).
  Q_INVOKABLE void updateInsertText(const QString& text);
  Q_INVOKABLE void commitInsertEditing();
  Q_INVOKABLE void cancelInsertEditing();
  // SEARCH — bound to the status bar's search field (ModeStatusBar.qml).
  Q_INVOKABLE void updateSearchQuery(const QString& query);
  Q_INVOKABLE void commitSearchEditing();
  Q_INVOKABLE void cancelSearchEditing();
  Q_INVOKABLE void shutdown();

 signals:
  void changed();
  void navigated();
  void shutdownFinished();

 private:
  friend struct DirectoryControllerTestAccess;
  std::function<void(const VimModeController::InsertCommitResult&)> before_commit_for_test_;
  void resetForNavigation();
  void listingChanged();
  void ensureSearchCurrent();
  quint64 listing_revision_ = 0;
  quint64 search_revision_ = 0;
  QString pre_search_name_;
  int takeCount();
  void setCursorRow(qint64 row);
  void clampCursorRow();
  void syncPreviewTarget();
  bool canPreviewSelection() const;
  void handleWorkerShutdown();
  QString entryNameAt(int proxyRow) const;
  void beginRename(VimModeController::InsertKind kind);
  void beginCreate(VimModeController::InsertKind kind);
  void removeActivePlaceholderIfAny();
  // The digit/"g"-chord/G/j/k motion parsing shared by NORMAL and VISUAL modes. Returns true when
  // key was recognized and consumed.
  bool handleCountAndMotionKeys(const QString& key, bool isDigit);
  // Everything reachable only from NORMAL mode: toggles, navigation, Quick Look, and the mode
  // transitions into VISUAL/INSERT/SEARCH. Returns true when key was recognized and consumed.
  bool handleNormalOnlyKey(const QString& key);
  bool handleNormalToggleAndNavigationKey(const QString& key);
  bool handleModeTransitionKey(const QString& key);
  // File-operations dispatch (SPEC.md file-operations): yy/dd chords and VISUAL y/d/D, checked
  // ahead of the rest of NORMAL/VISUAL dispatch. Returns true when key was recognized and
  // consumed.
  bool handleFileOperationKey(const QString& key, bool isVisual);
  // While tasks_.hasPrompt() is true, every key is either a valid prompt response or a swallowed
  // no-op (REQ-F-021's "paused... does not proceed until resolved" reads as exclusive key
  // capture) — takes priority over NORMAL/VISUAL/SEARCH entirely.
  bool handlePromptKey(const QString& key);
  bool eventFilter(QObject* watched, QEvent* event) override;
  QStringList collectVisualSelectionPaths() const;
  void yankOrCut(bool cut, bool wholeVisualSelection);
  void pasteRegister();
  void requestTrash(bool wholeVisualSelection);
  DirectoryModel model_;
  DirectoryProxyModel proxy_;
  PlacesModel places_;
  PreviewService preview_;
  VimModeController vim_;
  TaskManager tasks_;
  ClipboardRegister register_;
  QFileSystemWatcher watcher_;
  QString current_path_;
  QString status_message_;
  QString preview_target_path_;
  quint64 preview_revision_ = 0;
  int cursor_row_ = 0;
  int pending_count_ = 0;
  bool has_pending_count_ = false;
  bool pending_g_ = false;
  bool pending_y_ = false;
  bool pending_d_ = false;
  bool quick_look_open_ = false;
  int workers_finished_ = 0;
  int active_placeholder_source_row_ = -1;
  QElapsedTimer pending_g_timer_;
  QElapsedTimer pending_y_timer_;
  QElapsedTimer pending_d_timer_;
};
