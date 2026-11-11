#include "task_manager.h"

#include "native_operation_path.h"
#include "trash_service.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <cstdint>

namespace {

namespace trash = holonightd::trash;

using FailedItem = TaskManager::FailedItem;
using TaskSummary = TaskManager::TaskSummary;

// Mirrors DirectoryModel::acquireBatchSlot's polling idiom: waits for a prompt answer while
// staying responsive to cancellation. Returns false if the wait was aborted by Ctrl+C instead of
// an actual answer being supplied.
bool waitForPromptAnswer(QSemaphore* semaphore, const std::shared_ptr<std::atomic_bool>& cancel) {
  while (!cancel->load()) {
    if (semaphore->tryAcquire(1, 5)) {
      return !cancel->load();
    }
  }
  return false;
}

void postItemStarted(TaskManager* self, quint64 taskId, const QString& name) {
  QMetaObject::invokeMethod(self, [self, taskId, name] { self->onItemStarted(taskId, name); }, Qt::QueuedConnection);
}
void postItemProcessed(TaskManager* self, quint64 taskId) {
  QMetaObject::invokeMethod(self, [self, taskId] { self->onItemFinished(taskId); }, Qt::QueuedConnection);
}
void postConflictDetected(TaskManager* self, quint64 taskId, quint64 requestId, const QString& sourceName,
                          const QString& destName) {
  QMetaObject::invokeMethod(
      self,
      [self, taskId, requestId, sourceName, destName] {
        self->onConflictDetected(taskId, requestId, sourceName, destName);
      },
      Qt::QueuedConnection);
}

struct ConflictOutcome {
  enum class Action : std::uint8_t { Proceed, Skip, CancelTask, Aborted };
  Action action = Action::Proceed;
  QString dest_path;
  bool overwrite = false;
};

// Checks destDir/itemName for a collision and, if one exists, prompts and blocks for a
// resolution. Isolated from processCopyMoveTask's main loop purely to keep that loop's cognitive
// complexity under clang-tidy's threshold — no behavioral change from having it inline.
ConflictOutcome resolveConflictIfAny(TaskManager* self, QSemaphore* semaphore,
                                     const std::shared_ptr<std::atomic_bool>& cancel, std::atomic_int* pendingConflict,
                                     const QString& destDir, const QString& itemName, quint64 taskId,
                                     std::atomic<quint64>* promptSerial) {
  ConflictOutcome outcome;
  outcome.dest_path = QDir(destDir).filePath(itemName);
  if (!FileOperationService::destinationExists(destDir, itemName)) {
    return outcome;
  }
  pendingConflict->store(-1);
  postConflictDetected(self, taskId, ++(*promptSerial), QFile::decodeName(operationBytes(itemName)),
                       QFile::decodeName(operationBytes(itemName)));
  if (!waitForPromptAnswer(semaphore, cancel)) {
    outcome.action = ConflictOutcome::Action::Aborted;  // Ctrl+C while the prompt was open
    return outcome;
  }
  const auto resolution = static_cast<TaskManager::ConflictResolution>(pendingConflict->load());
  if (resolution == TaskManager::ConflictResolution::Skip) {
    outcome.action = ConflictOutcome::Action::Skip;  // REQ-F-022: destination untouched
    return outcome;
  }
  if (resolution == TaskManager::ConflictResolution::Cancel) {
    outcome.action = ConflictOutcome::Action::CancelTask;  // REQ-F-025: abort, drop the rest
    return outcome;
  }
  if (resolution == TaskManager::ConflictResolution::AutoRename) {
    outcome.dest_path = QDir(destDir).filePath(FileOperationService::autoRenameCandidate(destDir, itemName));
  } else {
    outcome.overwrite = true;  // Overwrite
  }
  return outcome;
}

TaskSummary processCopyMoveTask(const TaskManager::Task& task, const std::shared_ptr<std::atomic_bool>& cancel,
                                QSemaphore* semaphore, std::atomic_int* pendingConflict, TaskManager* self,
                                QSet<QString>* affectedDirs, std::atomic<quint64>* promptSerial) {
  TaskSummary summary;
  affectedDirs->insert(task.dest_dir);
  for (const auto& source : task.sources) {
    if (cancel->load()) {
      break;
    }
    const QFileInfo info(source);
    const auto itemName = info.fileName();
    postItemStarted(self, task.id, QFile::decodeName(operationBytes(itemName)));
    affectedDirs->insert(info.absolutePath());

    const auto outcome =
        resolveConflictIfAny(self, semaphore, cancel, pendingConflict, task.dest_dir, itemName, task.id, promptSerial);
    if (outcome.action == ConflictOutcome::Action::Aborted || outcome.action == ConflictOutcome::Action::CancelTask) {
      break;
    }
    if (outcome.action == ConflictOutcome::Action::Skip) {
      ++summary.skipped;
      postItemProcessed(self, task.id);
      continue;
    }

    const auto result = task.kind == TaskManager::TaskKind::Copy
                            ? FileOperationService::copyEntry(source, outcome.dest_path, outcome.overwrite, cancel)
                            : FileOperationService::moveEntry(source, outcome.dest_path, outcome.overwrite, cancel);
    if (result.complete()) {
      ++summary.succeeded;
    } else {
      ++summary.incomplete;
      if (result.failed || !result.nested_failures.isEmpty()) {
        ++summary.failed;
      }
      const auto reason = result.reason.isEmpty() ? QObject::tr("Transfer incomplete") : result.reason;
      summary.failures.append({.path = source, .reason = reason});
    }
    for (const auto& nested : result.nested_failures) {
      summary.failures.append({.path = nested.path, .reason = nested.reason});
    }
    summary.skipped_children.append(result.skipped_children);
    if (result.cancelled) {
      break;
    }
    postItemProcessed(self, task.id);
  }
  return summary;
}

TaskSummary processTrashTask(const TaskManager::Task& task, const std::shared_ptr<std::atomic_bool>& cancel,
                             TaskManager* self, QSet<QString>* affectedDirs) {
  TaskSummary summary;
  for (const auto& source : task.sources) {
    if (cancel->load()) {
      break;
    }
    postItemStarted(self, task.id, QFile::decodeName(operationBytes(QFileInfo(source).fileName())));
    affectedDirs->insert(QFileInfo(source).absolutePath());
    const auto result = TrashService::trashEntry(source, cancel);
    if (result.cancelled) {
      break;
    }
    if (result.failed) {
      ++summary.failed;
      ++summary.incomplete;
      summary.failures.append({.path = source, .reason = result.reason});
    } else {
      ++summary.succeeded;
    }
    postItemProcessed(self, task.id);
  }
  return summary;
}

TaskSummary processStoredTrashTask(const TaskManager::Task& task, const std::shared_ptr<std::atomic_bool>& cancel,
                                   TaskManager* self, QSet<QString>* affected) {
  TaskSummary summary;
  for (const auto& entry : task.trash_entries) {
    if (cancel->load()) {
      break;
    }
    const auto name = QFile::decodeName(entry.original_path.filename().c_str());
    postItemStarted(self, task.id, name);
    const auto destination = task.dest_dir.isEmpty()
                                 ? entry.original_path
                                 : std::filesystem::path(operationBytes(task.dest_dir).toStdString());
    const auto result =
        task.kind == TaskManager::TaskKind::Restore ? trash::restore(entry, destination) : trash::remove(entry);
    if (result) {
      ++summary.succeeded;
    } else {
      ++summary.failed;
      ++summary.incomplete;
      summary.failures.append({.path = name, .reason = QString::fromStdString(result.error())});
    }
    if (task.kind == TaskManager::TaskKind::Restore) {
      affected->insert(QFile::decodeName(destination.parent_path().c_str()));
    }
    postItemProcessed(self, task.id);
  }
  return summary;
}

}  // namespace

TaskManager::TaskManager(QObject* parent)
    : QObject(parent),
      worker_(new QObject),
      prompt_serial_(std::make_shared<std::atomic<quint64>>(0)),
      prompt_semaphore_(std::make_shared<QSemaphore>(0)),
      pending_conflict_resolution_(std::make_shared<std::atomic_int>(-1)) {
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, this, &TaskManager::shutdownFinished);
  thread_.start();
}

TaskManager::~TaskManager() {
  stopping_ = true;
  queue_.clear();
  resetPromptState();
  if (cancellation_) {
    cancellation_->store(true);
  }
  prompt_semaphore_->release();
  thread_.quit();
  thread_.wait();
}

QVariantList TaskManager::trashEntries() const {
  QVariantList rows;
  for (const auto& entry : trash_scan_.entries) {
    rows.push_back(QVariantMap{
        {QStringLiteral("name"), QFile::decodeName(entry.original_path.filename().c_str())},
        {QStringLiteral("location"), QFile::decodeName(entry.original_path.parent_path().c_str())},
        {
            QStringLiteral("date"),
            QDateTime::fromSecsSinceEpoch(entry.deleted_at.time_since_epoch().count()).toLocalTime(),
        },
        {
            QStringLiteral("volume"),
            entry.location.mount_point.empty() ? tr("Home Trash")
                                               : QFile::decodeName(entry.location.mount_point.c_str()),
        },
    });
  }
  return rows;
}

QStringList TaskManager::trashProblems() const {
  QStringList rows;
  for (const auto& problem : trash_scan_.problems) {
    rows.append(QString::fromStdString(problem));
  }
  return rows;
}

void TaskManager::scanTrash() {
  if (trash_scanning_ || stopping_) {
    return;
  }
  trash_scanning_ = true;
  emit trashChanged();
  QMetaObject::invokeMethod(
      worker_,
      [this] {
        auto scan = trash::scan();
        QMetaObject::invokeMethod(
            this,
            [this, scan = std::move(scan)] mutable {
              trash_scan_ = std::move(scan);
              trash_scanning_ = false;
              emit trashChanged();
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void TaskManager::restoreTrash(int index, const QUrl& folder, const QString& name) {
  if (stopping_ || busy_ || hasPrompt() || (trash_scanning_ && index != -1)) {
    return;
  }
  const auto position = static_cast<std::size_t>(index);
  const auto* entry = position < trash_scan_.entries.size() ? &trash_scan_.entries[position] : nullptr;
  if (entry == nullptr && index == -1 && !folder.isEmpty() && alternate_restore_) {
    entry = &*alternate_restore_;
  }
  if (entry == nullptr) {
    return;
  }
  QString destination;
  if (!folder.isEmpty()) {
    if (!folder.isLocalFile() || !QDir::isAbsolutePath(folder.toLocalFile()) || name.isEmpty() ||
        name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(u'/') ||
        name.contains(QChar::Null)) {
      return;
    }
    destination = QDir(folder.toLocalFile()).filePath(name);
  }
  original_restore_ = destination.isEmpty() ? std::optional(*entry) : std::nullopt;
  queue_.append({
      .kind = TaskKind::Restore,
      .sources = {},
      .dest_dir = destination,
      .id = ++next_task_id_,
      .trash_entries = {*entry},
  });
  alternate_restore_.reset();
  dispatchNextTask();
}

void TaskManager::confirmDeletion(std::vector<trash::Entry> entries) {
  if (entries.empty() || stopping_ || busy_ || hasPrompt() || trash_scanning_) {
    return;
  }
  queue_.append({
      .kind = TaskKind::DeletePermanently,
      .sources = {},
      .dest_dir = {},
      .id = ++next_task_id_,
      .trash_entries = std::move(entries),
  });
  dispatchNextTask();
}

void TaskManager::deleteTrash(int index) {
  const auto position = static_cast<std::size_t>(index);
  if (position < trash_scan_.entries.size()) {
    confirmDeletion({trash_scan_.entries[position]});
  }
}

void TaskManager::emptyTrash() { confirmDeletion(trash_scan_.entries); }

void TaskManager::enqueueCopy(const QStringList& sources, const QString& destDir) {
  if (sources.isEmpty() || stopping_) {
    return;
  }
  queue_.append(
      {.kind = TaskKind::Copy, .sources = sources, .dest_dir = destDir, .id = ++next_task_id_, .trash_entries = {}});
  dispatchNextTask();
}

void TaskManager::enqueueMove(const QStringList& sources, const QString& destDir) {
  if (sources.isEmpty() || stopping_) {
    return;
  }
  queue_.append(
      {.kind = TaskKind::Move, .sources = sources, .dest_dir = destDir, .id = ++next_task_id_, .trash_entries = {}});
  dispatchNextTask();
}

void TaskManager::requestTrashConfirmation(const QStringList& paths) {
  if (paths.isEmpty() || stopping_) {
    return;
  }
  queue_.append(
      {.kind = TaskKind::Trash, .sources = paths, .dest_dir = {}, .id = ++next_task_id_, .trash_entries = {}});
  dispatchNextTask();
}

void TaskManager::respondToTrashConfirm(bool confirmed, quint64 requestId) {
  if ((prompt_kind_ != PromptKind::TrashConfirm && prompt_kind_ != PromptKind::PermanentDeleteConfirm) ||
      requestId != prompt_id_) {
    return;
  }
  const auto task = queue_.takeFirst();
  resetPromptState();
  active_task_id_ = 0;
  if (confirmed) {
    startTask(task);
  } else {
    dispatchNextTask();
  }
  emit changed();
}

void TaskManager::resolveConflict(ConflictResolution resolution, quint64 requestId) {
  if (prompt_kind_ != PromptKind::Conflict || requestId != prompt_id_ || !cancellation_ || cancellation_->load()) {
    return;
  }
  if (resolution == ConflictResolution::Cancel) {
    cancelCurrentTask();
    return;
  }
  pending_conflict_resolution_->store(static_cast<int>(resolution));
  resetPromptState();
  prompt_semaphore_->release();
  emit changed();
}

void TaskManager::cancelCurrentTask() {
  if (cancellation_) {
    cancellation_->store(true);
  }
  prompt_semaphore_->release();
  queue_.clear();
  resetPromptState();
  if (!busy_) {
    active_task_id_ = 0;
  }
  emit changed();
}

void TaskManager::shutdown() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  queue_.clear();
  resetPromptState();
  if (cancellation_) {
    cancellation_->store(true);
  }
  prompt_semaphore_->release();
  thread_.quit();
}

void TaskManager::resetPromptState() {
  prompt_kind_ = PromptKind::None;
  conflict_source_name_.clear();
  conflict_dest_name_.clear();
  trash_confirm_count_ = 0;
  prompt_id_ = 0;
}

void TaskManager::dispatchNextTask() {
  if (busy_ || hasPrompt() || queue_.isEmpty() || stopping_) {
    return;
  }
  if (queue_.first().kind == TaskKind::Trash || queue_.first().kind == TaskKind::DeletePermanently) {
    current_operation_ = queue_.first().kind;
    active_task_id_ = queue_.first().id;
    trash_confirm_count_ = queue_.first().itemCount();
    prompt_id_ = ++(*prompt_serial_);
    last_prompt_id_ = prompt_id_;
    prompt_kind_ =
        current_operation_ == TaskKind::Trash ? PromptKind::TrashConfirm : PromptKind::PermanentDeleteConfirm;
    emit changed();
    return;
  }
  startTask(queue_.takeFirst());
}

void TaskManager::startTask(const Task& task) {
  active_task_id_ = task.id;
  busy_ = true;
  current_operation_ = task.kind;
  current_item_name_.clear();
  items_done_ = 0;
  items_total_ = task.itemCount();
  cancellation_ = std::make_shared<std::atomic_bool>(false);
  pending_conflict_resolution_ = std::make_shared<std::atomic_int>(-1);
  prompt_semaphore_ = std::make_shared<QSemaphore>(0);  // fresh per task: no stray cross-task tokens
  emit changed();
  runTaskOnWorker(task);
}

void TaskManager::runTaskOnWorker(const Task& task) {
  const auto cancel = cancellation_;
  const auto semaphore = prompt_semaphore_;
  const auto pendingConflict = pending_conflict_resolution_;
  const auto promptSerial = prompt_serial_;
  TaskManager* self = this;
  QMetaObject::invokeMethod(
      worker_,
      [self, task, cancel, semaphore, pendingConflict, promptSerial] {
        QSet<QString> affected;
        TaskSummary summary;
        switch (task.kind) {
          case TaskKind::Trash:
            summary = processTrashTask(task, cancel, self, &affected);
            break;
          case TaskKind::Restore:
          case TaskKind::DeletePermanently:
            summary = processStoredTrashTask(task, cancel, self, &affected);
            break;
          case TaskKind::Copy:
          case TaskKind::Move:
            summary = processCopyMoveTask(task, cancel, semaphore.get(), pendingConflict.get(), self, &affected,
                                          promptSerial.get());
            break;
        }
        const QStringList affectedList(affected.constBegin(), affected.constEnd());
        QMetaObject::invokeMethod(
            self,
            [self, taskId = task.id, summary, affectedList] { self->onTaskFinished(taskId, summary, affectedList); },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void TaskManager::onItemStarted(quint64 taskId, const QString& name) {
  if (taskId != active_task_id_ || !busy_ || cancellation_->load()) {
    return;
  }
  current_item_name_ = name;
  emit changed();
}

void TaskManager::onItemFinished(quint64 taskId) {
  if (taskId != active_task_id_ || !busy_ || cancellation_->load()) {
    return;
  }
  ++items_done_;
  emit changed();
}

void TaskManager::onConflictDetected(quint64 taskId, quint64 requestId, const QString& sourceName,
                                     const QString& destName) {
  if (taskId != active_task_id_ || !busy_ || stopping_ || cancellation_->load()) {
    return;
  }
  if (requestId <= last_prompt_id_) {
    return;
  }
  prompt_id_ = requestId;
  last_prompt_id_ = requestId;
  prompt_kind_ = PromptKind::Conflict;
  conflict_source_name_ = sourceName;
  conflict_dest_name_ = destName;
  emit changed();
}

void TaskManager::onTaskFinished(quint64 taskId, TaskSummary summary, QStringList affectedDirs) {
  if (taskId != active_task_id_) {
    return;
  }
  resetPromptState();
  active_task_id_ = 0;
  busy_ = false;
  last_summary_ = std::move(summary);
  formatSummaryText();
  if (current_operation_ == TaskKind::Restore || current_operation_ == TaskKind::DeletePermanently) {
    if (original_restore_ && last_summary_.failed > 0) {
      alternate_restore_ = std::move(original_restore_);
      emit restoreAlternativeRequested(QFile::decodeName(alternate_restore_->original_path.filename().c_str()),
                                       last_summary_.failures.front().reason);
    }
    original_restore_.reset();
    scanTrash();
  }
  emit taskFinished(std::move(affectedDirs));
  emit changed();
  dispatchNextTask();
}

void TaskManager::formatSummaryText() {
  const auto& summary = last_summary_;
  const int total = summary.succeeded + summary.incomplete + summary.skipped;
  QString text = tr("%1/%2 succeeded").arg(summary.succeeded).arg(total);
  if (!summary.failures.isEmpty()) {
    QStringList parts;
    parts.reserve(summary.failures.size());
    for (const auto& failure : summary.failures) {
      parts << QStringLiteral("%1 (%2)").arg(QFileInfo(failure.path).fileName(), failure.reason);
    }
    text += tr(". Details: %1").arg(parts.join(QStringLiteral(", ")));
  }
  if (summary.incomplete > 0) {
    text += tr(". %1 incomplete").arg(summary.incomplete);
  }
  if (summary.skipped > 0) {
    text += tr(". %1 skipped").arg(summary.skipped);
  }
  if (!summary.skipped_children.isEmpty()) {
    text += tr(". Skipped children: %1").arg(summary.skipped_children.join(QStringLiteral(", ")));
  }
  last_summary_text_ = std::move(text);
}
