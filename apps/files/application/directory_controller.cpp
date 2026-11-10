#include "directory_controller.h"

#include "file_transfer.h"
#include "native_operation_path.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPersistentModelIndex>

#include <HolonightFileBrowser/grid_navigation.h>

namespace {
Holonight::NavigationCommand navigationMotion(const FileCommand& command) {
  using Kind = FileCommand::Kind;
  using N = Holonight::NavigationCommand;
  switch (command.kind) {
    case Kind::Move:
      return command.count < 0 ? N::Previous : N::Next;
    case Kind::MoveHorizontal:
      return command.count < 0 ? N::Left : N::Right;
    case Kind::First:
      return N::First;
    case Kind::Last:
      return N::Last;
    case Kind::Page:
      return command.count < 0 ? N::PageUp : N::PageDown;
    default:
      return command.count < 0 ? N::HalfPageUp : N::HalfPageDown;
  }
}
}  // namespace

DirectoryController::DirectoryController(QObject* parent) : DirectoryController(nullptr, parent) {}
DirectoryController::DirectoryController(HolonightFileBrowser::ImageRuntimeOptions runtime, QObject* parent)
    : DirectoryController(nullptr, parent, {}, std::move(runtime)) {}
DirectoryController::DirectoryController(HoloNight::System::StorageController* storage, QObject* parent,
                                         std::shared_ptr<const CapacityProbe> capacity)
    : DirectoryController(storage, parent, std::move(capacity), HolonightFileBrowser::defaultImageRuntimeOptions()) {}
DirectoryController::DirectoryController(HoloNight::System::StorageController* storage, QObject* parent,
                                         std::shared_ptr<const CapacityProbe> capacity,
                                         HolonightFileBrowser::ImageRuntimeOptions runtime)
    : QObject(parent),
      devices_(storage != nullptr ? new DevicesModel(storage, this, std::move(capacity)) : new DevicesModel(this)),
      preview_(std::move(runtime)) {
  connect(&input_preferences_, &Holonight::InputPreferences::profileChanged, this,
          &DirectoryController::resetNavigationInput);
  connect(this, &DirectoryController::navigated, this,
          [this] { breadcrumbs_.setPath(QFile::encodeName(currentPath())); });
  connect(devices_, &DevicesModel::openRequested, this, [this](const QString& path) { open(path); });
  connect(devices_, &DevicesModel::operationFailed, this, [this](const QString& message) {
    status_message_ = message;
    emit changed();
  });
  connect(devices_, &DevicesModel::recoveryRequested, this,
          [this] { open(QDir::homePath(), tr("Storage was unmounted or disconnected. Returned to Home.")); });
  connect(this, &DirectoryController::changed, this,
          [this] { devices_->setInteractionEnabled(sidebarActivationEnabled()); });
  QCoreApplication::instance()->installEventFilter(&window_events_);
  navigation_.proxy_.setSourceModel(&navigation_.model_);
  navigation_.model_.setPlaceIcons(places_.placeIcons());
  // Ahead of the forwarding connection, so observers never see a settled listing whose cursor has
  // not yet been restored.
  connect(&navigation_.model_, &DirectoryModel::changed, this, &DirectoryController::maybeApplyPendingRestore);
  connect(&navigation_.model_, &DirectoryModel::changed, this, &DirectoryController::syncSelection);
  connect(&navigation_.model_, &DirectoryModel::changed, this, &DirectoryController::changed);
  connect(&navigation_.model_, &DirectoryModel::shutdownFinished, this,
          [this] { lifecycle_.workerFinished(&navigation_.model_); });
  connect(&navigation_.model_, &DirectoryModel::locationResolved, devices_, &DevicesModel::resolvedLocation);
  connect(&navigation_.model_, &DirectoryModel::restoreValidated, this, &DirectoryController::handleRestoreValidated);
  connect(&places_, &PlacesModel::bookmarkRecheckResolved, this, &DirectoryController::handleBookmarkRecheckResolved);
  connect(&navigation_.model_, &DirectoryModel::loadSucceeded, this,
          [this](const QString& path, LocationClassifier::Classification classification) {
            lifecycle_.last_location_tracker_.recordLoad(path, classification);
          });
  connect(&finder_, &PathFinderModel::shutdownFinished, this, [this] { lifecycle_.workerFinished(&finder_); });
  connect(&preview_, &PreviewService::shutdownFinished, this, [this] { lifecycle_.workerFinished(&preview_); });
  connect(&vim_, &VimModeController::changed, this, &DirectoryController::changed);
  connect(&tasks_, &TaskManager::changed, this, [this] {
    if (tasks_.hasPrompt()) {
      commands_.reset();
    }
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
    finder_.invalidatePaths(affectedDirs);
    status_message_ = tasks_.lastSummaryText();
    devices_->refreshCapacity();  // A copy or delete just changed free space somewhere.
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
  connect(&navigation_.watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
    finder_.invalidatePaths({path});
    ++preview_selection_.preview_revision_;
    navigation_.model_.refresh();
  });
  connect(&navigation_.watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
    finder_.invalidatePaths({path});
    ++preview_selection_.preview_revision_;
    navigation_.model_.refresh();
  });
}
bool DirectoryController::sidebarActivationEnabled() const {
  return vim_.currentMode() == VimModeController::Mode::Normal && !tasks_.hasPrompt() && !quickLookOpen();
}
void DirectoryController::open(const QString& path, const QString& fallbackReason) {
  openInternal(path, fallbackReason, /*restoreName=*/{}, /*recordHistory=*/true);
}
void DirectoryController::activateBreadcrumb(int index) {
  if (!sidebarActivationEnabled()) {
    return;
  }
  const auto path = breadcrumbs_.nativePathAt(index);
  if (!path.isEmpty()) {
    open(QFile::decodeName(path));
  }
}
bool DirectoryController::acceptFinderResult(int row) {
  const auto path = finder_.pathAt(row);
  if (path.isEmpty()) {
    return false;
  }
  const QFileInfo info(path);
  if (!info.exists() || info.isDir() != finder_.isDirectoryAt(row)) {
    finder_.reportMissing(path);
    return false;
  }
  if (info.isDir()) {
    open(path);
  } else {
    openInternal(info.absolutePath(), {}, info.fileName(), true);
  }
  return true;
}
void DirectoryController::openInternal(const QString& requestedPath, const QString& fallbackReason,
                                       const QString& restoreName, bool recordHistory) {
  navigation_.openInternal(requestedPath, fallbackReason, restoreName, recordHistory);
}
QString DirectoryController::outgoingCursorName() const { return navigation_.outgoingCursorName(); }
bool DirectoryController::historyNavigationEnabled() const { return sidebarActivationEnabled() && !finder_open_; }
bool DirectoryController::canGoUp() const { return !currentPath().isEmpty() && !QDir(currentPath()).isRoot(); }
void DirectoryController::goUp() {
  takeCount();
  if (historyNavigationEnabled() && canGoUp()) {
    navigateParent();
  }
}
void DirectoryController::setIconView(bool icons) {
  if (!transfersEnabled() || icons == icon_view_) {
    return;
  }
  const QPersistentModelIndex source = listing()->mapToSource(listing()->index(cursorRow(), 0));
  icon_view_ = icons;
  const auto target = listing()->mapFromSource(source);
  navigation_.cursor_.setIndex(target.isValid() ? target.row() : 0);
  ++editing_.listing_revision_;
  vim_.updateSearchPositions(entryNameAt(cursorRow()));
  emit iconViewChanged();
  emit changed();
}
void DirectoryController::setGridDimensions(int columns, int rows) {
  const int width = qMax(1, columns);
  viewport_rows_ = qMax(1, rows);
  if (grid_columns_ != width) {
    grid_columns_ = width;
    emit changed();
  }
}
void DirectoryController::selectEntry(int row, int modifiers) {
  if (transfersEnabled() && row >= 0 && row < listing()->rowCount()) {
    vim_.selection()->pointer(row, (modifiers & Qt::ControlModifier) != 0, (modifiers & Qt::ShiftModifier) != 0);
    setCursorRow(row);
  }
}
void DirectoryController::activateEntry(int row) {
  if (historyNavigationEnabled()) {
    openEntry(row);
  }
}
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
void DirectoryController::copyCursorPath() {
  QString path = navigation_.current_path_;
  const int row = navigation_.cursor_.index();
  if (directoryError().isEmpty() && row >= 0 && row < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(row, 0));
    const auto rowPath = navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
    if (!rowPath.isEmpty()) {
      path = rowPath;
    }
  }
  if (path.isEmpty()) {
    return;
  }
  if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
    clipboard->setText(path);
  }
  status_message_ = tr("Copied path: %1").arg(path);
  emit changed();
}
QString DirectoryController::cursorPath() const {
  QString path = navigation_.current_path_;
  const int row = navigation_.cursor_.index();
  if (directoryError().isEmpty() && row >= 0 && row < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(row, 0));
    const auto rowPath = navigation_.model_.data(sourceIndex, DirectoryModel::PathRole).toString();
    if (!rowPath.isEmpty()) {
      path = rowPath;
    }
  }
  const QString home = QDir::cleanPath(QDir::homePath());
  if (path == home) {
    return QStringLiteral("~/");
  }
  if (home != QLatin1String("/") && path.startsWith(home + u'/')) {
    return QStringLiteral("~") + path.mid(home.size());
  }
  return path;
}
QString DirectoryController::entryNameAt(int proxyRow) const { return navigation_.entryNameAt(proxyRow); }
void DirectoryController::beginRename(VimModeController::InsertKind kind) { editing_.beginRename(kind); }
void DirectoryController::beginCreate(VimModeController::InsertKind kind) { editing_.beginCreate(kind); }
void DirectoryController::removeActivePlaceholderIfAny() { editing_.removeActivePlaceholderIfAny(); }

QStringList DirectoryController::collectVisualSelectionPaths() const {
  QStringList paths;
  for (const auto& path : vim_.selection()->selected()) {
    paths.append(operationPath(path));
  }
  return paths;
}
void DirectoryController::yankOrCut(bool cut, bool wholeVisualSelection) {
  QStringList paths;
  if (vim_.selectedCount() > 0) {
    paths = collectVisualSelectionPaths();
  } else if (navigation_.cursor_.index() >= 0 && navigation_.cursor_.index() < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(navigation_.cursor_.index(), 0));
    const auto path = navigation_.model_.data(sourceIndex, DirectoryModel::NativePathRole).toByteArray();
    if (!path.isEmpty() && entryNameAt(cursorRow()) != u"..") {
      paths = {operationPath(path)};
    }
  }
  if (wholeVisualSelection || vim_.selectedCount() > 0) {
    vim_.exitVisual();
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
  if (vim_.selectedCount() > 0) {
    paths = collectVisualSelectionPaths();
  } else if (navigation_.cursor_.index() >= 0 && navigation_.cursor_.index() < navigation_.proxy_.rowCount()) {
    const auto sourceIndex = navigation_.proxy_.mapToSource(navigation_.proxy_.index(navigation_.cursor_.index(), 0));
    const auto path = navigation_.model_.data(sourceIndex, DirectoryModel::NativePathRole).toByteArray();
    if (!path.isEmpty() && entryNameAt(cursorRow()) != u"..") {
      paths = {operationPath(path)};
    }
  }
  if (wholeVisualSelection || vim_.selectedCount() > 0) {
    vim_.exitVisual();
  }
  if (paths.isEmpty()) {
    return;
  }
  tasks_.requestTrashConfirmation(paths);  // REQ-F-016: unconditional, single vs. VISUAL alike
}
void DirectoryController::updateInsertText(const QString& text) { editing_.updateInsertText(text); }
void DirectoryController::commitInsertEditing() { editing_.commitInsertEditing(); }
void DirectoryController::cancelInsertEditing() { editing_.cancelInsertEditing(); }
void DirectoryController::syncSelection() {
  QList<QByteArray> visible;
  QList<QByteArray> ineligible;
  for (int row = 0; row < listing()->rowCount(); ++row) {
    const auto index = listing()->index(row, 0);
    const auto nativePath = index.data(DirectoryModel::NativePathRole).toByteArray();
    visible.append(nativePath);
    if (index.data(DirectoryModel::NameRole).toString() == u".." || nativePath.isEmpty()) {
      ineligible.append(nativePath);
    }
  }
  vim_.selection()->setVisibleEntries(visible, ineligible);
  QList<QByteArray> available;
  for (int row = 0; row < navigation_.model_.rowCount(); ++row) {
    available.append(navigation_.model_.index(row, 0).data(DirectoryModel::NativePathRole).toByteArray());
  }
  vim_.selection()->reconcile(available, !scanning() && directoryError().isEmpty());
}
void DirectoryController::listingChanged() {
  syncSelection();
  editing_.listingChanged();
}
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
  const auto mode = vim_.currentMode();
  if (transfersEnabled()) {
    if (key == u"Ctrl+V") {
      vim_.selection()->beginMark(cursorRow());
      return true;
    }
    if (key == u"Escape" && (vim_.selectedCount() > 0 || mode == VimModeController::Mode::Mark)) {
      vim_.exitVisual();
      return true;
    }
    if (mode == VimModeController::Mode::Mark && key == u" ") {
      vim_.selection()->toggle(cursorRow());
      return true;
    }
    if (mode == VimModeController::Mode::Mark && (key == u"v" || key == u"V")) {
      vim_.enterVisual(cursorRow());
      return true;
    }
  }
  const auto command = commands_.route(key, {
                                                .mode = vim_.currentMode(),
                                                .prompt = tasks_.promptKind(),
                                                .quick_look_open = quickLookOpen(),
                                                .can_preview = canPreviewSelection(),
                                                .icon_view = icon_view_,
                                                .vim_navigation = input_preferences_.vimNavigation(),
                                            });
  execute(command);
  return command.consumed;
}
void DirectoryController::execute(const FileCommand& command) {
  using Kind = FileCommand::Kind;
  switch (command.kind) {
    case Kind::None:
      break;
    case Kind::Move:
    case Kind::MoveHorizontal:
    case Kind::First:
    case Kind::Last:
    case Kind::Page:
    case Kind::HalfPage: {
      setCursorRow(Holonight::navigationTarget(cursorRow(),
                                               {.command = navigationMotion(command), .count = qAbs(command.count)},
                                               {.count = listing()->rowCount(),
                                                .columns = icon_view_ ? grid_columns_ : 1,
                                                .viewportHeight = static_cast<qreal>(viewport_rows_),
                                                .rect = {}}));
      break;
    }
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
    case Kind::MoveQuickLookLine:
      // Only the viewer's current line moves; the listing did not change, so no changed() and no retarget.
      preview_.moveCurrentLine(command.count);
      break;
    case Kind::SwapVisualEnd: {
      const int target = vim_.visualAnchorRow();
      vim_.swapVisualEnds();
      setCursorRow(target);
      break;
    }
    case Kind::FindFile:
      emit finderRequested(false);
      break;
    case Kind::ChangeDirectory:
      emit finderRequested(true);
      break;
    case Kind::Visual:
      cancelPendingRestore();
      if (listing()->rowCount() == 0) {
        break;
      }
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

bool DirectoryController::transfersEnabled() const {
  const auto mode = vim_.currentMode();
  return (mode == VimModeController::Mode::Normal || mode == VimModeController::Mode::Visual ||
          mode == VimModeController::Mode::Mark) &&
         !tasks_.hasPrompt() && !quickLookOpen() && !finder_open_ && !lifecycle_.started_ && directoryError().isEmpty();
}
QString DirectoryController::dragUriList(int row) const {
  if (!transfersEnabled()) {
    return {};
  }
  const auto source = navigation_.proxy_.index(row, 0);
  if (!source.isValid() || source.data(DirectoryModel::NativePathRole).toByteArray().isEmpty() ||
      entryNameAt(row) == u"..") {
    return {};
  }
  QStringList paths;
  if (vim_.isRowSelected(row)) {
    paths = collectVisualSelectionPaths();
  } else {
    paths.append(operationPath(source.data(DirectoryModel::NativePathRole).toByteArray()));
  }
  QString result;
  for (const auto& path : paths) {
    result +=
        QString::fromLatin1(HolonightFileBrowser::localUrl(operationBytes(path)).toEncoded()) + QStringLiteral("\r\n");
  }
  return result;
}
QString DirectoryController::dropDestination(int row) const {
  const auto index = navigation_.proxy_.index(row, 0);
  if (index.isValid() && index.data(DirectoryModel::IsDirRole).toBool()) {
    const auto path = index.data(DirectoryModel::PathRole).toString();
    if (!path.isEmpty()) {
      return path;
    }
  }
  return currentPath();
}
bool DirectoryController::submitDrop(const QList<QUrl>& urls, int row, int action) {
  if (!transfersEnabled()) {
    return false;
  }
  const auto paths = FileTransfer::localPaths(urls);
  if (paths.isEmpty()) {
    return false;
  }
  return FileTransfer::submit(tasks_, paths, dropDestination(row), action);
}
