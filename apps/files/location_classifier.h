#pragma once

#include <QString>

// Mount facts for one path, as QStorageInfo reports them.
struct MountInfo {
  QString fs_type;
  QString device;  // e.g. "/dev/sdz1"; empty for pseudo filesystems
  QString mount_root;
  bool valid = false;  // false: the path's mount could not be resolved (SPEC.md REQ-F-026)
};

// Decides whether a folder may be remembered and restored (SPEC.md REQ-F-022). Replaceable so a
// future MountService, or a test, can stand in. classify() may block on a hung mount, so it is only
// ever called on DirectoryModel's worker thread (REQ-F-016).
class LocationClassifier {
 public:
  enum class Classification { Local, Network, Removable };
  LocationClassifier() = default;
  virtual ~LocationClassifier() = default;
  LocationClassifier(const LocationClassifier&) = delete;
  LocationClassifier& operator=(const LocationClassifier&) = delete;
  LocationClassifier(LocationClassifier&&) = delete;
  LocationClassifier& operator=(LocationClassifier&&) = delete;
  virtual Classification classify(const QString& path) const = 0;
};

class RealLocationClassifier : public LocationClassifier {
 public:
  explicit RealLocationClassifier(QString sysfsRoot = QStringLiteral("/sys"));
  Classification classify(const QString& path) const override;

 private:
  QString sysfs_root_;
};

// The pure classification rule, fed injected mount facts and a (possibly fake) sysfs root.
// An unresolvable mount is reported as Network: it cannot be shown to be local, and anything other
// than Local is never stored or restored.
LocationClassifier::Classification classifyMount(const QString& path, const MountInfo& mount, const QString& sysfsRoot);
