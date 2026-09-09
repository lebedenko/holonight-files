#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

struct DirectoryEntry {
  QString name;
  QString absolute_path;
  bool is_dir = false;
  qint64 size = -1;
  QDateTime modified;
  quint32 mode = 0;
  bool stat_failed = false;
  QString stat_error;
  bool operator==(const DirectoryEntry&) const = default;
};

// Raw, unsorted listing of one directory's immediate entries. Populated asynchronously on a
// worker thread so a large or slow (network-mounted) directory never blocks the UI thread.
// Sorting and hidden-file filtering are the DirectoryProxyModel's job, not this model's.
class DirectoryModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    PathRole,
    IsDirRole,
    SizeRole,
    ModifiedRole,
    ModeRole,
    IsHiddenRole,
    StatFailedRole,
    StatErrorRole,
  };
  explicit DirectoryModel(QObject* parent = nullptr);
  ~DirectoryModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  bool scanning() const { return scanning_; }
  QString directoryPath() const { return directory_path_; }
  QString directoryError() const { return directory_error_; }
  // Starts a fresh walk of path, discarding any current contents immediately.
  void load(const QString& path);
  // Re-walks the current directoryPath(), diffing the result against current rows instead of
  // resetting the model, so watcher-driven refreshes keep scroll position and sort work intact.
  void refresh();
  void shutdown();
 signals:
  void changed();
  void shutdownFinished();

 private:
  friend struct DirectoryModelTestAccess;
  // Snapshotted on the UI thread before dispatch; no public filesystem abstraction.
  std::function<void()> before_open_for_test_;
  int read_error_after_for_test_ = -1;
  struct Batch {
    quint64 generation = 0;
    bool diff = false;
    QList<DirectoryEntry> entries;
    bool finished = false;
    QString directory_error;
  };
  void startWalk(const QString& path, bool diff);
  void applyBatch(const Batch& batch);
  void appendEntries(const QList<DirectoryEntry>& entries);
  void applyDiffEntries(const QList<DirectoryEntry>& entries);
  void finishDiff();
  QThread thread_;
  QObject* worker_;
  QList<DirectoryEntry> entries_;
  QHash<QString, int> name_to_row_;
  QSet<QString> seen_this_refresh_;
  QString directory_path_;
  QString directory_error_;
  std::shared_ptr<std::atomic_bool> cancellation_;
  quint64 generation_ = 0;
  bool scanning_ = false;
  bool walk_in_flight_ = false;
  bool stopping_ = false;
};
