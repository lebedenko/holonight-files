#pragma once

#include <QAbstractListModel>
#include <QThread>

#include <HolonightFileBrowser/directory_entry.h>
#include <HolonightFileBrowser/directory_roles.h>
#include <HolonightFileBrowser/places/place_list.h>
#include <atomic>
#include <memory>

namespace HolonightFileBrowser {
// An independent current-folder reader. Application navigation, selection, watchers,
// editing and persistence belong to the consumer. Paths must be absolute.
class DirectoryReader : public QAbstractListModel, public DirectoryRoles {
  Q_OBJECT
  Q_PROPERTY(QString directoryPath READ directoryPath NOTIFY changed)
  Q_PROPERTY(QByteArray nativeDirectoryPath READ nativeDirectoryPath NOTIFY changed)
  Q_PROPERTY(QString directoryError READ directoryError NOTIFY changed)
  Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
 public:
  explicit DirectoryReader(QObject* parent = nullptr);
  ~DirectoryReader() override;
  DirectoryReader(const DirectoryReader&) = delete;
  DirectoryReader& operator=(const DirectoryReader&) = delete;
  DirectoryReader(DirectoryReader&&) = delete;
  DirectoryReader& operator=(DirectoryReader&&) = delete;
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] QString directoryPath() const { return path_; }
  [[nodiscard]] QByteArray nativeDirectoryPath() const { return native_path_; }
  [[nodiscard]] QString directoryError() const { return error_; }
  [[nodiscard]] bool scanning() const { return scanning_; }
  Q_INVOKABLE void load(const QString& path);
  // Local Linux filenames need not be valid UTF-8. Native bytes are never decoded for filesystem I/O.
  Q_INVOKABLE void loadNative(const QByteArray& path);
  // Cancel delivery and retire the worker asynchronously. Keep the reader alive until
  // shutdownFinished before destroying it when a blocking filesystem may be involved.
  Q_INVOKABLE void shutdown();
  void setPlaceIcons(std::shared_ptr<const PlaceList::IconMap> icons);
 signals:
  void changed();
  void shutdownFinished();

 private:
  void startWalk();
  QThread thread_;
  QObject* worker_;
  QList<DirectoryEntry> entries_;
  std::shared_ptr<std::atomic_bool> cancel_;
  std::shared_ptr<const PlaceList::IconMap> icons_;
  quint64 generation_ = 0;
  QString path_;
  QByteArray native_path_;
  QString error_;
  bool scanning_ = false;
  bool stopping_ = false;
};
}  // namespace HolonightFileBrowser
