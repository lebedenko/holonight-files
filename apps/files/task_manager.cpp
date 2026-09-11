#include "task_manager.h"

#include "trash_service.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <cstdint>

namespace {

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
  postConflictDetected(self, taskId, ++(*promptSerial), itemName, itemName);
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
  affectedDirs->insert(task.destDir);
  for (const auto& source : task.sources) {
    if (cancel->load()) {
      break;
    }
    const QFileInfo info(source);
    const auto itemName = info.fileName();
    postItemStarted(self, task.id, itemName);
    affectedDirs->insert(info.absolutePath());

    const auto outcome =
        resolveConflictIfAny(self, semaphore, cancel, pendingConflict, task.destDir, itemName, task.id, promptSerial);
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
      if (result.failed || !result.nestedFailures.isEmpty()) {
        ++summary.failed;
      }
      const auto reason = result.reason.isEmpty() ? QObject::tr("Transfer incomplete") : result.reason;
      summary.failures.append({.path = source, .reason = reason});
    }
    for (const auto& nested : result.nestedFailures) {
      summary.failures.append({.path = nested.path, .reason = nested.reason});
    }
    summary.skippedChildren.append(result.skippedChildren);
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
    postItemStarted(self, task.id, QFileInfo(source).fileName());
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

void TaskManager::enqueueCopy(const QStringList& sources, const QString& destDir) {
  if (sources.isEmpty() || stopping_) {
    return;
  }
  queue_.append({.kind = TaskKind::Copy, .sources = sources, .destDir = destDir, .id = ++next_task_id_});
  dispatchNextTask();
}

void TaskManager::enqueueMove(const QStringList& sources, const QString& destDir) {
  if (sources.isEmpty() || stopping_) {
    return;
  }
  queue_.append({.kind = TaskKind::Move, .sources = sources, .destDir = destDir, .id = ++next_task_id_});
  dispatchNextTask();
}

void TaskManager::requestTrashConfirmation(const QStringList& paths) {
  if (paths.isEmpty() || stopping_) {
    return;
  }
  queue_.append({.kind = TaskKind::Trash, .sources = paths, .destDir = {}, .id = ++next_task_id_});
  dispatchNextTask();
}

void TaskManager::respondToTrashConfirm(bool confirmed, quint64 requestId) {
  if (prompt_kind_ != PromptKind::TrashConfirm || requestId != prompt_id_) {
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
  trash_confirm_paths_.clear();
  prompt_id_ = 0;
}

void TaskManager::dispatchNextTask() {
  if (busy_ || hasPrompt() || queue_.isEmpty() || stopping_) {
    return;
  }
  if (queue_.first().kind == TaskKind::Trash) {
    active_task_id_ = queue_.first().id;
    trash_confirm_paths_ = queue_.first().sources;
    prompt_id_ = ++(*prompt_serial_);
    last_prompt_id_ = prompt_id_;
    prompt_kind_ = PromptKind::TrashConfirm;
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
  items_total_ = static_cast<int>(task.sources.size());
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
        const auto summary = task.kind == TaskKind::Trash
                                 ? processTrashTask(task, cancel, self, &affected)
                                 : processCopyMoveTask(task, cancel, semaphore.get(), pendingConflict.get(), self,
                                                       &affected, promptSerial.get());
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
  if (!summary.skippedChildren.isEmpty()) {
    text += tr(". Skipped children: %1").arg(summary.skippedChildren.join(QStringLiteral(", ")));
  }
  last_summary_text_ = text;
}
