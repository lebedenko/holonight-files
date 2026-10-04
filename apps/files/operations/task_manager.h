#pragma once

#include "file_operation_service.h"

#include <QList>
#include <QObject>
#include <QSemaphore>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>
#include <memory>

// The queue, progress, cancellation, and prompt engine for copy/move/trash (SPEC.md's TaskManager,
// DESIGN.md's Components section). Owned as a value member of DirectoryController, exposed as
// `Q_PROPERTY(TaskManager* tasks READ tasks CONSTANT)` — structurally identical to how
// preview()/vim() are exposed today. Owns one persistent worker QObject moved to one dedicated
// QThread, started in the constructor and torn down in the destructor/shutdown(), mirroring
// DirectoryModel and PreviewService (see [[async-worker-thread-pattern]]).
class TaskManager : public QObject {
  Q_OBJECT

  Q_PROPERTY(TaskKind currentOperation READ currentOperation NOTIFY changed)
  Q_PROPERTY(QString currentItemName READ currentItemName NOTIFY changed)
  Q_PROPERTY(int itemsDone READ itemsDone NOTIFY changed)
  Q_PROPERTY(int itemsTotal READ itemsTotal NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)

  Q_PROPERTY(bool hasPrompt READ hasPrompt NOTIFY changed)
  Q_PROPERTY(PromptKind promptKind READ promptKind NOTIFY changed)
  Q_PROPERTY(QString conflictSourceName READ conflictSourceName NOTIFY changed)
  Q_PROPERTY(QString conflictDestName READ conflictDestName NOTIFY changed)
  Q_PROPERTY(int trashConfirmCount READ trashConfirmCount NOTIFY changed)
  Q_PROPERTY(quint64 promptId READ promptId NOTIFY changed)

  Q_PROPERTY(QString lastSummaryText READ lastSummaryText NOTIFY changed)

 public:
  // Nested (like PreviewService's PreviewErrorKind) so QML resolves these as TaskManager.Copy,
  // TaskManager.Conflict, etc.
  enum class TaskKind : std::uint8_t { Copy, Move, Trash };
  Q_ENUM(TaskKind)
  enum class ConflictResolution : std::uint8_t { Skip, Overwrite, AutoRename, Cancel };
  Q_ENUM(ConflictResolution)
  enum class PromptKind : std::uint8_t { None, Conflict, TrashConfirm };
  Q_ENUM(PromptKind)

  struct FailedItem {
    QString path;
    QString reason;
  };

  struct TaskSummary {
    int succeeded = 0;
    int failed = 0;
    int incomplete = 0;
    int skipped = 0;
    QStringList skipped_children;
    QList<FailedItem> failures;
  };

  struct Task {
    TaskKind kind = TaskKind::Copy;
    QStringList sources;
    QString dest_dir;
    quint64 id = 0;
  };

  explicit TaskManager(QObject* parent = nullptr);
  ~TaskManager() override;
  TaskManager(const TaskManager&) = delete;
  TaskManager& operator=(const TaskManager&) = delete;
  TaskManager(TaskManager&&) = delete;
  TaskManager& operator=(TaskManager&&) = delete;

  [[nodiscard]] TaskKind currentOperation() const { return current_operation_; }
  [[nodiscard]] QString currentItemName() const { return current_item_name_; }
  [[nodiscard]] int itemsDone() const { return items_done_; }
  [[nodiscard]] int itemsTotal() const { return items_total_; }
  [[nodiscard]] bool busy() const { return busy_; }

  [[nodiscard]] bool hasPrompt() const { return prompt_kind_ != PromptKind::None; }
  [[nodiscard]] PromptKind promptKind() const { return prompt_kind_; }
  [[nodiscard]] QString conflictSourceName() const { return conflict_source_name_; }
  [[nodiscard]] QString conflictDestName() const { return conflict_dest_name_; }
  [[nodiscard]] int trashConfirmCount() const { return static_cast<int>(trash_confirm_paths_.size()); }
  [[nodiscard]] quint64 promptId() const { return prompt_id_; }

  [[nodiscard]] QString lastSummaryText() const { return last_summary_text_; }
  [[nodiscard]] const TaskSummary& lastSummary() const { return last_summary_; }

  Q_INVOKABLE void enqueueCopy(const QStringList& sources, const QString& destDir);
  Q_INVOKABLE void enqueueMove(const QStringList& sources, const QString& destDir);
  Q_INVOKABLE void requestTrashConfirmation(const QStringList& paths);
  Q_INVOKABLE void respondToTrashConfirm(bool confirmed, quint64 requestId);
  Q_INVOKABLE void resolveConflict(ConflictResolution resolution, quint64 requestId);
  Q_INVOKABLE void cancelCurrentTask();
  void shutdown();

  // Called back (via queued invokeMethod) from the free worker-side functions in
  // task_manager.cpp's anonymous namespace while task processing runs on the worker thread. Not
  // part of the QML-facing API (no Q_INVOKABLE) — public only because those callbacks are plain
  // functions, not members, and so cannot be friended individually.
  void onItemStarted(quint64 taskId, const QString& name);
  void onItemFinished(quint64 taskId);
  void onConflictDetected(quint64 taskId, quint64 requestId, const QString& sourceName, const QString& destName);
  void onTaskFinished(quint64 taskId, TaskSummary summary, QStringList affectedDirs);

 signals:
  void changed();
  // Emitted once a queued task (Copy/Move/Trash) fully drains — DirectoryController refreshes any
  // directory listed here in addition to its existing QFileSystemWatcher-driven refresh.
  void taskFinished(QStringList affectedDirs);
  void shutdownFinished();

 private:
  friend struct TaskManagerTestAccess;
  void dispatchNextTask();
  void startTask(const Task& task);
  void runTaskOnWorker(const Task& task);
  void resetPromptState();
  void formatSummaryText();

  QThread thread_;
  QObject* worker_;

  QList<Task> queue_;
  TaskKind current_operation_ = TaskKind::Copy;
  QString current_item_name_;
  int items_done_ = 0;
  int items_total_ = 0;
  bool busy_ = false;

  PromptKind prompt_kind_ = PromptKind::None;
  QString conflict_source_name_;
  QString conflict_dest_name_;
  QStringList trash_confirm_paths_;
  quint64 prompt_id_ = 0;
  quint64 last_prompt_id_ = 0;
  quint64 active_task_id_ = 0;
  quint64 next_task_id_ = 0;
  std::shared_ptr<std::atomic<quint64>> prompt_serial_;

  TaskSummary last_summary_;
  QString last_summary_text_;

  // Shared with the in-flight worker task; replaced for every new task.
  std::shared_ptr<std::atomic_bool> cancellation_;
  std::shared_ptr<QSemaphore> prompt_semaphore_;
  std::shared_ptr<std::atomic_int> pending_conflict_resolution_;

  bool stopping_ = false;
};
