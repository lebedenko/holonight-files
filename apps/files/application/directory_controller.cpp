#include "directory_controller.h"

#include <QCoreApplication>

DirectoryController::DirectoryController(QObject* parent) : QObject(parent) {
  QCoreApplication::instance()->installEventFilter(&window_events_);
  navigation_.proxy_.setSourceModel(&navigation_.model_);
  // Ahead of the forwarding connection, so observers never see a settled listing whose cursor has
  // not yet been restored.
  connect(&navigation_.model_, &DirectoryModel::changed, this, &DirectoryController::maybeApplyPendingRestore);
  connect(&navigation_.model_, &DirectoryModel::changed, this, &DirectoryController::changed);
  connect(&navigation_.model_, &DirectoryModel::shutdownFinished, this,
          [this] { lifecycle_.workerFinished(&navigation_.model_); });
  connect(&navigation_.model_, &DirectoryModel::restoreValidated, this, &DirectoryController::handleRestoreValidated);
  connect(&places_, &PlacesModel::bookmarkRecheckResolved, this, &DirectoryController::handleBookmarkRecheckResolved);
  connect(&navigation_.model_, &DirectoryModel::loadSucceeded, this,
          [this](const QString& path, LocationClassifier::Classification classification) {
            lifecycle_.last_location_tracker_.recordLoad(path, classification);
          });
  connect(&preview_, &PreviewService::shutdownFinished, this, [this] { lifecycle_.workerFinished(&preview_); });
  connect(&vim_, &VimModeController::changed, this, &DirectoryController::changed);
  connect(&tasks_, &TaskManager::changed, this, [this] {
    if (tasks_.hasPrompt() && preview_selection_.quick_look_open_) {
      preview_selection_.quick_look_open_ = false;
      preview_.setQuickLookActive(false);
    }
    emit changed();
  });
  connect(&tasks_, &TaskManager::shutdownFinished, this, [this] { lifecycle_.workerFinished(&tasks_); });
  connect(&tasks_, &TaskManager::taskFinished, this, [this](const QStringList& affectedDirs) {
    // Renders through the same normalStatusLabel/statusMessage channel open()'s fallbackReason
    // already uses (REQ-F-034/035) — no new ModeStatusBar branch needed for the summary itself.
    status_message_ = tasks_.lastSummaryText();
    // In addition to (not instead of) the existing QFileSystemWatcher-driven refresh, so the
    // listing updates deterministically right after a paste/trash rather than only whenever the
    // watcher happens to coalesce a filesystem event.
    if (affectedDirs.contains(navigation_.current_path_)) {
      navigation_.model_.refresh();
    }
  });
  // changed() already fires on every cursor move, batch flush, watcher-driven refresh, and
  // toggle; syncPreviewTarget() guards on the resolved path so it's cheap when nothing
  // preview-relevant actually changed.
  connect(this, &DirectoryController::changed, this, &DirectoryController::syncPreviewTarget);
  connect(&navigation_.proxy_, &QAbstractItemModel::rowsInserted, this, &DirectoryController::listingChanged);
  connect(&navigation_.proxy_, &QAbstractItemModel::rowsRemoved, this, &DirectoryController::listingChanged);
  connect(&navigation_.proxy_, &QAbstractItemModel::modelReset, this, &DirectoryController::listingChanged);
  connect(&navigation_.proxy_, &QAbstractItemModel::layoutChanged, this, &DirectoryController::listingChanged);
  connect(&navigation_.proxy_, &QAbstractItemModel::dataChanged, this, &DirectoryController::listingChanged);
  connect(&navigation_.watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
    ++preview_selection_.preview_revision_;
    navigation_.model_.refresh();
  });
  connect(&navigation_.watcher_, &QFileSystemWatcher::fileChanged, this, [this] {
    ++preview_selection_.preview_revision_;
    navigation_.model_.refresh();
  });
}
void DirectoryController::open(const QString& path, const QString& fallbackReason) {
  openInternal(path, fallbackReason, /*restoreName=*/{}, /*recordHistory=*/true);
}
void DirectoryController::openInternal(const QString& requestedPath, const QString& fallbackReason,
                                       const QString& restoreName, bool recordHistory) {
  navigation_.openInternal(requestedPath, fallbackReason, restoreName, recordHistory);
}
QString DirectoryController::outgoingCursorName() const { return navigation_.outgoingCursorName(); }
void DirectoryController::goBack() {
  takeCount();  // a click always means one step, whatever digits were typed (REQ-F-033)
  traverseHistory(-1, 1);
}
void DirectoryController::goForward() {
  takeCount();
  traverseHistory(1, 1);
}
void DirectoryController::navigateHistoryBack() { traverseHistory(-1, takeCount()); }
void DirectoryController::navigateHistoryForward() { traverseHistory(1, takeCount()); }
void DirectoryController::traverseHistory(int direction, int count) { navigation_.traverseHistory(direction, count); }
void DirectoryController::cancelPendingRestore() { navigation_.cancelPendingRestore(); }
void DirectoryController::maybeApplyPendingRestore() { navigation_.maybeApplyPendingRestore(); }
void DirectoryController::navigateInto(int proxyRow) { navigation_.navigateInto(proxyRow); }
void DirectoryController::navigateParent() { navigation_.navigateParent(); }
void DirectoryController::openEntry(int proxyRow) { navigation_.openEntry(proxyRow); }
void DirectoryController::toggleHidden() {
  if (vim_.currentMode() != VimModeController::Mode::Insert) {
    navigation_.proxy_.setHiddenVisible(!navigation_.proxy_.hiddenVisible());
  }
}
void DirectoryController::toggleSortDirection() {
  if (vim_.currentMode() != VimModeController::Mode::Insert) {
    navigation_.proxy_.setSortDescending(!navigation_.proxy_.sortDescending());
  }
}

void DirectoryController::setCursorRow(qint64 row) { navigation_.setCursorRow(row); }
void DirectoryController::clampCursorRow() { navigation_.clampCursorRow(); }
bool DirectoryController::canPreviewSelection() const { return preview_selection_.canPreviewSelection(); }
QString DirectoryController::entryNameAt(int proxyRow) const { return navigation_.entryNameAt(proxyRow); }
void DirectoryController::beginRename(VimModeController::InsertKind kind) { editing_.beginRename(kind); }
void DirectoryController::beginCreate(VimModeController::InsertKind kind) { editing_.beginCreate(kind); }
void DirectoryController::removeActivePlaceholderIfAny() { editing_.removeActivePlaceholderIfAny(); }

QStringList DirectoryController::collectVisualSelectionPaths() const {
  QStringList paths;
  for (int row = 0; row < navigation_.proxy_.rowCount(); ++row) {
    if (!vim_.isRowSelected(row)) {
      continue;
    }
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(row, 0));
    const auto path = navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}
void DirectoryController::yankOrCut(bool cut, bool wholeVisualSelection) {
  QStringList paths;
  if (wholeVisualSelection) {
    paths = collectVisualSelectionPaths();
    vim_.exitVisual();  // REQ-F-002/004: exits VISUAL immediately, regardless of what was selected
  } else if (navigation_.cursor_row_ >= 0 && navigation_.cursor_row_ < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(navigation_.cursor_row_, 0));
    const auto path = navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
    if (!path.isEmpty()) {
      paths = {path};
    }
  }
  if (paths.isEmpty()) {
    return;
  }
  register_.paths = paths;  // REQ-F-005: silently overwrites whatever was registered before
  register_.cut = cut;
}
void DirectoryController::pasteRegister() {
  if (register_.paths.isEmpty() || navigation_.current_path_.isEmpty()) {
    return;
  }
  const auto paths = register_.paths;
  const bool cut = register_.cut;
  if (cut) {
    register_.clear();  // REQ-F-008: cleared synchronously with this keypress, not on completion
    tasks_.enqueueMove(paths, navigation_.current_path_);
  } else {
    tasks_.enqueueCopy(paths, navigation_.current_path_);
  }
}
void DirectoryController::requestTrash(bool wholeVisualSelection) {
  QStringList paths;
  if (wholeVisualSelection) {
    paths = collectVisualSelectionPaths();
    vim_.exitVisual();  // REQ-F-015: exits VISUAL immediately, prompt appears back in NORMAL
  } else if (navigation_.cursor_row_ >= 0 && navigation_.cursor_row_ < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(navigation_.cursor_row_, 0));
    const auto path = navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
    if (!path.isEmpty()) {
      paths = {path};
    }
  }
  if (paths.isEmpty()) {
    return;
  }
  tasks_.requestTrashConfirmation(paths);  // REQ-F-016: unconditional, single vs. VISUAL alike
}
void DirectoryController::updateInsertText(const QString& text) { editing_.updateInsertText(text); }
void DirectoryController::commitInsertEditing() { editing_.commitInsertEditing(); }
void DirectoryController::cancelInsertEditing() { editing_.cancelInsertEditing(); }
void DirectoryController::listingChanged() { editing_.listingChanged(); }
void DirectoryController::ensureSearchCurrent() { editing_.ensureSearchCurrent(); }
void DirectoryController::updateSearchQuery(const QString& query) { editing_.updateSearchQuery(query); }
void DirectoryController::commitSearchEditing() { editing_.commitSearchEditing(); }
void DirectoryController::cancelSearchEditing() { editing_.cancelSearchEditing(); }
void DirectoryController::resetForNavigation() {
  vim_.cancelInsert();
  removeActivePlaceholderIfAny();
  navigation_.proxy_.setEditing(false);
  vim_.exitVisual();
  vim_.resetSearch();
  editing_.pre_search_name_.clear();
  commands_.reset();
  preview_selection_.quick_look_open_ = false;
  preview_.setQuickLookActive(false);
  ++editing_.listing_revision_;
}
void DirectoryController::syncPreviewTarget() { preview_selection_.syncPreviewTarget(); }

void DirectoryController::activateBookmark(int placesRow) { navigation_.activateBookmark(placesRow); }
void DirectoryController::handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available) {
  navigation_.handleBookmarkRecheckResolved(placeId, path, available);
}
void DirectoryController::openRestoreCandidate(const QString& path) { navigation_.openRestoreCandidate(path); }
void DirectoryController::handleRestoreValidated(const QString& path, RestoreOutcome outcome) {
  navigation_.handleRestoreValidated(path, outcome);
}
void DirectoryController::saveState() { lifecycle_.saveState(); }
void DirectoryController::shutdown() { lifecycle_.shutdown(); }

int DirectoryController::takeCount() { return commands_.takeCount(); }
bool DirectoryController::handleKey(const QString& key) {
  const auto command = commands_.route(key, {.mode = vim_.currentMode(),
                                             .prompt = tasks_.promptKind(),
                                             .quickLookOpen = quickLookOpen(),
                                             .canPreview = canPreviewSelection()});
  execute(command);
  return command.consumed;
}
void DirectoryController::execute(const FileCommand& command) {
  using Kind = FileCommand::Kind;
  switch (command.kind) {
    case Kind::None:
      break;
    case Kind::Move:
      setCursorRow(qint64{cursorRow()} + command.count);
      break;
    case Kind::First:
      setCursorRow(0);
      break;
    case Kind::Last:
      setCursorRow(listing()->rowCount() - 1);
      break;
    case Kind::ToggleHidden:
      toggleHidden();
      break;
    case Kind::ToggleSort:
      toggleSortDirection();
      break;
    case Kind::Parent:
      navigateParent();
      break;
    case Kind::Open:
      openEntry(cursorRow());
      break;
    case Kind::ToggleQuickLook:
    case Kind::CloseQuickLook:
      preview_selection_.quick_look_open_ = command.kind == Kind::ToggleQuickLook && !quickLookOpen();
      preview_.setQuickLookActive(quickLookOpen());
      emit changed();
      break;
    case Kind::Visual:
      cancelPendingRestore();
      vim_.enterVisual(cursorRow());
      break;
    case Kind::ExitVisual:
      vim_.exitVisual();
      break;
    case Kind::RenamePrepend:
      beginRename(VimModeController::InsertKind::Prepend);
      break;
    case Kind::RenameAppend:
      beginRename(VimModeController::InsertKind::Append);
      break;
    case Kind::CreateBelow:
      beginCreate(VimModeController::InsertKind::CreateBelow);
      break;
    case Kind::CreateAbove:
      beginCreate(VimModeController::InsertKind::CreateAbove);
      break;
    case Kind::Search:
      cancelPendingRestore();
      editing_.pre_search_name_ = entryNameAt(cursorRow());
      vim_.enterSearch(cursorRow());
      break;
    case Kind::NextMatch:
    case Kind::PreviousMatch: {
      ensureSearchCurrent();
      const int row = vim_.advanceSearchMatch(cursorRow(), command.kind == Kind::NextMatch);
      if (row >= 0) {
        setCursorRow(row);
      }
      break;
    }
    case Kind::Yank:
      yankOrCut(false, command.visual);
      break;
    case Kind::Cut:
      yankOrCut(true, command.visual);
      break;
    case Kind::Trash:
      requestTrash(command.visual);
      break;
    case Kind::Paste:
      pasteRegister();
      break;
    case Kind::Skip:
      tasks_.resolveConflict(TaskManager::ConflictResolution::Skip, tasks_.promptId());
      break;
    case Kind::Overwrite:
      tasks_.resolveConflict(TaskManager::ConflictResolution::Overwrite, tasks_.promptId());
      break;
    case Kind::AutoRename:
      tasks_.resolveConflict(TaskManager::ConflictResolution::AutoRename, tasks_.promptId());
      break;
    case Kind::CancelConflict:
      tasks_.resolveConflict(TaskManager::ConflictResolution::Cancel, tasks_.promptId());
      break;
    case Kind::ConfirmTrash:
      tasks_.respondToTrashConfirm(command.count != 0, tasks_.promptId());
      break;
  }
}
