#pragma once
#include "clipboard_register.h"
#include "directory_model.h"
#include "directory_proxy_model.h"
#include "editing_session.h"
#include "file_command_router.h"
#include "jump_list.h"
#include "navigation_session.h"
#include "places_model.h"
#include "preview_selection.h"
#include "preview_service.h"
#include "session_lifecycle.h"
#include "state/last_location_tracker.h"
#include "state/state_store.h"
#include "task_manager.h"
#include "vim_mode_controller.h"
#include "warning_sink.h"
#include "window_event_filter.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// QML-facing application coordinator. Private sessions own navigation, editing, preview
// selection, command parsing and lifecycle state; invokables retain the application contract.
class DirectoryController : public QObject {
  Q_OBJECT
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
  Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
  Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
 public:
  explicit DirectoryController(QObject* parent = nullptr);
  QString currentPath() const { return navigation_.current_path_; }
  QString statusMessage() const { return status_message_; }
  QString directoryError() const { return navigation_.model_.directoryError(); }
  bool scanning() const { return navigation_.model_.scanning(); }
  int cursorRow() const { return navigation_.cursor_row_; }
  DirectoryProxyModel* listing() { return &navigation_.proxy_; }
  PlacesModel* places() { return &places_; }
  PreviewService* preview() { return &preview_; }
  VimModeController* vim() { return &vim_; }
  TaskManager* tasks() { return &tasks_; }
  bool quickLookOpen() const { return preview_selection_.quick_look_open_; }
  bool canGoBack() const { return navigation_.jump_list_.canGoBack(); }
  bool canGoForward() const { return navigation_.jump_list_.canGoForward(); }
  Q_INVOKABLE void open(const QString& path, const QString& fallbackReason = {});
  // Bookmark activation entry point (SPEC.md REQ-F-022): PlaceRow calls this instead of open()
  // for origin === Bookmark rows. Home/XDG rows keep calling open(path) directly (unchanged --
  // both are guaranteed available whenever shown, REQ-C-005/REQ-C-006).
  Q_INVOKABLE void activateBookmark(int placesRow);
  Q_INVOKABLE void navigateInto(int proxyRow);
  Q_INVOKABLE void navigateParent();
  Q_INVOKABLE void openEntry(int proxyRow);
  // Header buttons: always one step, discarding any pending vim count (REQ-F-033).
  Q_INVOKABLE void goBack();
  Q_INVOKABLE void goForward();
  // Ctrl+O / Ctrl+I: consume the pending vim count as the step count (REQ-F-007/011/023).
  Q_INVOKABLE void navigateHistoryBack();
  Q_INVOKABLE void navigateHistoryForward();
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
  // C++-only startup wiring from main() (SPEC.md REQ-C-006). While enabled, shutdown() saves the
  // session's last Local folder to state.toml (REQ-F-017/018).
  void configureRestore(bool enabled) { lifecycle_.restore_enabled_ = enabled; }
  // Validates path on the worker thread, then opens it, or home with the matching fallback reason
  // (REQ-F-012/013). Discarded if any other navigation happens first.
  void openRestoreCandidate(const QString& path);

 signals:
  void changed();
  void navigated();
  void shutdownFinished();

 private:
  friend class NavigationSession;
  friend class EditingSession;
  friend class PreviewSelection;
  friend class SessionLifecycle;
  friend struct DirectoryControllerTestAccess;

  void resetForNavigation();
  // restoreName is the entry the cursor lands on once the new listing settles; empty means row 0.
  // recordHistory is false only for history traversal, which has already moved the jump list.
  void openInternal(const QString& requestedPath, const QString& fallbackReason, const QString& restoreName,
                    bool recordHistory);
  void traverseHistory(int direction, int count);
  QString outgoingCursorName() const;
  void maybeApplyPendingRestore();
  void cancelPendingRestore();
  void listingChanged();
  void ensureSearchCurrent();

  int takeCount();
  void setCursorRow(qint64 row);
  void clampCursorRow();
  void syncPreviewTarget();
  bool canPreviewSelection() const;

  void handleRestoreValidated(const QString& path, RestoreOutcome outcome);
  void handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available);
  void saveState();
  QString entryNameAt(int proxyRow) const;
  void beginRename(VimModeController::InsertKind kind);
  void beginCreate(VimModeController::InsertKind kind);
  void removeActivePlaceholderIfAny();
  QStringList collectVisualSelectionPaths() const;
  void yankOrCut(bool cut, bool wholeVisualSelection);
  void pasteRegister();
  void requestTrash(bool wholeVisualSelection);

  PlacesModel places_;
  PreviewService preview_;
  VimModeController vim_;
  TaskManager tasks_;
  ClipboardRegister register_;

  QString status_message_;

  NavigationSession navigation_{*this};
  EditingSession editing_{*this};
  PreviewSelection preview_selection_{*this};
  SessionLifecycle lifecycle_{*this};
  FileCommandRouter commands_;
  WindowEventFilter window_events_{*this};
  void execute(const FileCommand& command);
};
