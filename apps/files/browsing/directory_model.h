#pragma once

#include "location_classifier.h"
#include "places/place_list.h"
#include "restore_outcome.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QThread>

#include <HolonightFileBrowser/directory_entry.h>
#include <HolonightFileBrowser/directory_roles.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

// Shared worker metadata; application-only placeholder rows use the same value type.
// Raw, unsorted listing of one directory's immediate entries. Populated asynchronously on a
// worker thread so a large or slow (network-mounted) directory never blocks the UI thread.
// Sorting and hidden-file filtering are the DirectoryProxyModel's job, not this model's.
class DirectoryModel : public QAbstractListModel, public DirectoryRoles {
  Q_OBJECT
 public:
  explicit DirectoryModel(QObject* parent = nullptr);
  ~DirectoryModel() override;
  DirectoryModel(const DirectoryModel&) = delete;
  DirectoryModel& operator=(const DirectoryModel&) = delete;
  DirectoryModel(DirectoryModel&&) = delete;
  DirectoryModel& operator=(DirectoryModel&&) = delete;
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool scanning() const { return scanning_; }
  [[nodiscard]] QString directoryPath() const { return directory_path_; }
  [[nodiscard]] QString directoryError() const { return directory_error_; }
  // Starts a fresh walk of path, discarding any current contents immediately.
  void load(const QString& path);
  // Re-walks the current directoryPath(), diffing the result against current rows instead of
  // resetting the model, so watcher-driven refreshes keep scroll position and sort work intact.
  void refresh();
  void suspendUpdates();
  void resumeUpdates();
  // INSERT-mode (o/O) create placeholder row (SPEC.md REQ-F-010/011): always appended, so no
  // other row's index shifts. Synchronous, UI-thread-only — no worker involvement (REQ-F-041).
  // Returns the new row's index. DirectoryProxyModel is responsible for sorting it to the
  // requested visual position (see DirectoryProxyModel::setPlaceholder()).
  int insertPlaceholderRow();
  // No-op if row is out of range or isn't a placeholder row.
  void removePlaceholderRow(int row);
  void shutdown();
  // The synthetic ".." row shown until the walker delivers the parent folder's stat'ed entry. `places` may be
  // null (generic chain).
  static DirectoryEntry syntheticParentEntry(const QString& path, const PlaceList::IconMap* places = nullptr);
  // Installs the immutable place->icon map used by walks started after this call (load()/refresh()). Rows
  // already in the model are not re-resolved; callers refresh() to apply it. GUI thread only. C++-only on
  // purpose (no Q_INVOKABLE/Q_PROPERTY/role). A null pointer restores generic-only behaviour.
  void setPlaceIcons(std::shared_ptr<const PlaceList::IconMap> icons);
  // Stats and classifies a stored last location on the worker thread (SPEC.md REQ-F-016) and
  // reports the result through restoreValidated().
  void validateForRestore(const QString& path);
 signals:
  void changed();
  void shutdownFinished();
  void restoreValidated(const QString& path, RestoreOutcome outcome);
  // A load() (never a refresh()) whose successful final batch was accepted by the UI;
  // classification may arrive after refresh, navigation or shutdown begins (REQ-F-019).
  void locationResolved(const QString& path, const QString& canonicalPath);
  void loadSucceeded(const QString& path, LocationClassifier::Classification classification);

 private:
  friend struct DirectoryModelTestAccess;
  // Snapshotted on the UI thread before dispatch; no public filesystem abstraction.
  std::function<void()> before_open_for_test_;
  int read_error_after_for_test_ = -1;
  // Shared with in-flight worker tasks, so replacing it never invalidates a running call.
  std::shared_ptr<const LocationClassifier> classifier_;
  std::shared_ptr<const PlaceList::IconMap> place_icons_;  // null until set
  struct Batch {
    quint64 generation = 0;
    bool diff = false;
    QList<DirectoryEntry> entries;
    bool finished = false;
    QString directory_error;
  };
  void resolveLocation(const QString& path, quint64 generation);
  void startWalk(const QString& path, bool diff);
  void applyBatch(const Batch& batch);
  // Applies a load batch's leading ".." entry to row 0 in place and returns the remaining entries.
  QList<DirectoryEntry> replaceParentRow(QList<DirectoryEntry> entries);
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
  bool updates_suspended_ = false;
  bool scanning_ = false;
  bool walk_in_flight_ = false;
  bool stopping_ = false;
};
