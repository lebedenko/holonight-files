#include "location_classifier.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

#include <algorithm>
#include <array>
#include <utility>

namespace {
using Classification = LocationClassifier::Classification;

// REQ-F-023's denylist plus other common FUSE network backends.
constexpr std::array kNetworkFilesystems = {
    "nfs",
    "nfs4",
    "cifs",
    "smb3",
    "smbfs",
    "9p",
    "afs",
    "ceph",
    "glusterfs",
    "davfs",
    "lustre",
    "fuse.sshfs",
    "fuse.rclone",
    "fuse.gvfsd-fuse",
    "fuse.s3fs",
    "fuse.davfs2",
    "fuse.curlftpfs",
    "fuse.glusterfs",
    "fuse.ceph-fuse",
    "fuse.smbnetfs",
    "fuse.gcsfuse",
    "fuse.goofys",
    "fuse.juicefs",
    "fuse.kio-fuse",
};

bool isUnder(const QString& path, const QString& root) {
  return path == root || path.startsWith(root.endsWith(u'/') ? root : root + u'/');
}

bool isNetwork(const QString& path, const MountInfo& mount) {
  if (std::ranges::any_of(kNetworkFilesystems,
                          [&](const char* type) { return mount.fs_type.compare(QLatin1String(type)) == 0; })) {
    return true;
  }
  static const QRegularExpression gvfs(QStringLiteral("^/run/user/\\d+/gvfs(/|$)"));
  return gvfs.match(path).hasMatch() || gvfs.match(mount.mount_root).hasMatch();
}

bool readsOne(const QString& file) {
  QFile removable(file);
  return removable.open(QIODevice::ReadOnly) && removable.readAll().trimmed() == "1";
}

// Maps a partition device to the whole-disk directory that carries the removable flag.
bool deviceIsRemovable(const QString& device, const QString& sysfsRoot) {
  if (!device.startsWith(QStringLiteral("/dev/"))) {
    return false;
  }
  const auto name = QFileInfo(device).fileName();
  const QString block = sysfsRoot + QStringLiteral("/block/");
  if (QFileInfo::exists(block + name + QStringLiteral("/removable"))) {
    return readsOne(block + name + QStringLiteral("/removable"));
  }
  // /sys/class/block/<partition> links into its parent disk's directory.
  const auto canonical = QFileInfo(sysfsRoot + QStringLiteral("/class/block/") + name).canonicalFilePath();
  if (!canonical.isEmpty() && QFileInfo::exists(canonical + QStringLiteral("/../removable"))) {
    return readsOne(QDir::cleanPath(canonical + QStringLiteral("/../removable")));
  }
  // sdz1 -> sdz, nvme0n1p2 -> nvme0n1, mmcblk0p1 -> mmcblk0
  static const QRegularExpression partitionSuffix(QStringLiteral("^(.*\\d)p\\d+$|^(.*\\D)\\d+$"));
  const auto match = partitionSuffix.match(name);
  if (match.hasMatch()) {
    const auto disk = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    return readsOne(block + disk + QStringLiteral("/removable"));
  }
  return false;
}
}  // namespace

Classification classifyMount(const QString& path, const MountInfo& mount, const QString& sysfsRoot) {
  if (!mount.valid) {
    return Classification::Network;
  }
  if (isNetwork(QDir::cleanPath(path), mount)) {
    return Classification::Network;
  }
  if (isUnder(mount.mount_root, QStringLiteral("/run/media")) || isUnder(mount.mount_root, QStringLiteral("/media")) ||
      deviceIsRemovable(mount.device, sysfsRoot)) {
    return Classification::Removable;
  }
  return Classification::Local;
}

RealLocationClassifier::RealLocationClassifier(QString sysfsRoot) : sysfs_root_(std::move(sysfsRoot)) {}

Classification RealLocationClassifier::classify(const QString& path) const {
  const QStorageInfo storage(path);
  MountInfo mount;
  mount.valid = storage.isValid();
  if (mount.valid) {
    mount.fs_type = QString::fromUtf8(storage.fileSystemType());
    mount.device = QString::fromUtf8(storage.device());
    // /dev/mapper/<name> and /dev/disk/by-* are symlinks to the kernel's device name.
    const auto target = QFileInfo(mount.device).canonicalFilePath();
    if (!target.isEmpty()) {
      mount.device = target;
    }
    mount.mount_root = storage.rootPath();
  }
  return classifyMount(path, mount, sysfs_root_);
}
