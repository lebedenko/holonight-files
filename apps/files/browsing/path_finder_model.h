#pragma once

#include "path_scanner.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <holonight/search/index.h>
#include <memory>

class PathFinderModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
  Q_PROPERTY(QString homePath READ homePath CONSTANT)
  Q_PROPERTY(QString query READ query NOTIFY stateChanged)
  Q_PROPERTY(bool directoriesOnly READ directoriesOnly NOTIFY stateChanged)
  Q_PROPERTY(bool includeHidden READ includeHidden NOTIFY stateChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
  Q_PROPERTY(int indexedCount READ indexedCount NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
 public:
  enum Role { PathRole = Qt::UserRole + 1, RelativePathRole, DirectoryRole, PositionsRole };
  explicit PathFinderModel(QObject* parent = nullptr);
  PathFinderModel(PathScanFunction scanner, QObject* parent = nullptr);
  ~PathFinderModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  QString rootPath() const { return root_path_; }
  static QString homePath();
  QString query() const { return query_; }
  bool directoriesOnly() const { return directories_only_; }
  bool includeHidden() const { return include_hidden_; }
  bool scanning() const { return scanning_; }
  int indexedCount() const { return indexed_count_; }
  QString error() const {
    return config_error_.isEmpty() ? error_ : config_error_ + (error_.isEmpty() ? QString{} : u'\n' + error_);
  }
  Q_INVOKABLE void start(const QString& rootPath, bool directoriesOnly);
  Q_INVOKABLE void setRoot(const QString& rootPath);
  Q_INVOKABLE void setQuery(const QString& query);
  Q_INVOKABLE void setIncludeHidden(bool includeHidden);
  Q_INVOKABLE void stop();
  Q_INVOKABLE QString pathAt(int row) const;
  Q_INVOKABLE bool isDirectoryAt(int row) const;
  void warmUp();
  void invalidatePaths(const QStringList& paths);
  void shutdown();
  void setClock(std::function<qint64()> clock);
  // Injectable clock/interval for deterministic freshness tests.
  void setRefreshInterval(int milliseconds);
  void reportMissing(const QString& path);
 signals:
  void stateChanged();
  void shutdownFinished();

 private:
  struct Row {
    QString path;
    QString relativePath;
    bool directory = false;
    QList<int> positions;
  };
  void reloadSearchSettings();
  void scan();
  void launchScan(const QString& root, bool hidden);
  void scheduleWork();
  void refreshIfNeeded();
  static QString stateKey(const QString& root, bool hidden);
  void checkSelectedRoot(quint64 selection);
  void clearUnavailableRoot(const QString& root, bool hidden, quint64 selection);
  void loadSnapshot(const QString& root, quint64 serial, const SearchExclusionPolicy& policy,
                    const std::shared_ptr<std::atomic_bool>& cancel, const QString& cachePath);
  void runScan(const QString& root, quint64 serial, bool includeHidden, const std::shared_ptr<std::atomic_bool>& cancel,
               const SearchExclusionPolicy& policy, bool persistent, bool load, const QString& cachePath);
  void rank();
  void publishScanBatch(const QString& root, quint64 serial, bool includeHidden,
                        const std::shared_ptr<HolonightSearch::Index>& index, int count);
  void finishScan(const QString& root, quint64 serial, bool includeHidden,
                  const std::shared_ptr<HolonightSearch::Index>& index, bool available);
  static QVector<Row> rowsForHits(const QVector<HolonightSearch::Result>& hits, const QString& root,
                                  bool directoriesOnly, const QSet<QString>& missing);
  void replaceRows(QVector<Row> rows);
  QString root_path_;
  QString query_;
  QString error_;
  QString config_error_;
  SearchExclusionPolicy policy_;
  bool policy_loaded_ = false;
  bool directories_only_ = false;
  bool include_hidden_ = false;
  bool scanning_ = false;
  int indexed_count_ = 0;
  quint64 scan_serial_ = 0;
  quint64 rank_serial_ = 0;
  std::shared_ptr<std::atomic_bool> scan_cancel_;
  std::shared_ptr<std::atomic_bool> rank_cancel_;
  QHash<QString, std::shared_ptr<HolonightSearch::Index>> visible_cache_;
  QHash<QString, std::shared_ptr<HolonightSearch::Index>> hidden_cache_;
  QSet<QString> complete_visible_roots_;
  QSet<QString> complete_hidden_roots_;
  QHash<QString, QSet<QString>> missing_paths_;
  QVector<Row> rows_;
  struct Freshness {
    qint64 completed = 0;
    bool dirty = true;
  };
  QHash<QString, Freshness> freshness_;
  bool check_root_ = false;
  quint64 selection_serial_ = 0;
  bool active_ = false;
  bool warming_ = false;
  bool warmed_ = false;
  bool shutting_down_ = false;
  bool job_running_ = false;
  QString job_root_;
  bool job_hidden_ = false;
  QString pending_root_;
  bool pending_hidden_ = false;
  int refresh_interval_ = 300000;
  std::function<qint64()> clock_;
  QThreadPool pool_;
  QThreadPool rank_pool_;
  QTimer refresh_timer_;
  QTimer invalidation_timer_;
  QTimer shutdown_timer_;
  QTimer rank_timer_;
  PathScanFunction scanner_;
};
