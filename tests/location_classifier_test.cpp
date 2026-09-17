#include "location_classifier.h"

#include "directory_fixtures.h"

#include <QDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using Classification = LocationClassifier::Classification;

namespace {
MountInfo mount(const QString& fsType, const QString& device, const QString& root) {
  return {.fs_type = fsType, .device = device, .mount_root = root, .valid = true};
}

// A stand-in for /sys with one disk, sdz, whose removable flag the test chooses.
struct FakeSysfs {
  explicit FakeSysfs(const char* removable) : dir(fixturePattern("sysfs")) {
    QDir().mkpath(dir.filePath("block/sdz"));
    files_test::writeFile(dir, "block/sdz/removable", removable);
  }
  [[nodiscard]] QString root() const { return dir.path(); }
  QTemporaryDir dir;
};
}  // namespace

class NetworkFilesystems : public testing::TestWithParam<const char*> {};

TEST_P(NetworkFilesystems, AreClassifiedNetwork) {
  const FakeSysfs sysfs("0");
  EXPECT_EQ(classifyMount("/mnt/share/dir", mount(GetParam(), "server:/export", "/mnt/share"), sysfs.root()),
            Classification::Network);
}

INSTANTIATE_TEST_SUITE_P(LocationClassifier, NetworkFilesystems,
                         testing::Values("nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "afs", "ceph", "glusterfs",
                                         "davfs", "fuse.sshfs", "fuse.rclone", "fuse.gvfsd-fuse"));

TEST(LocationClassifier, GvfsPathIsNetworkWhateverTheFilesystemType) {
  const FakeSysfs sysfs("0");
  EXPECT_EQ(classifyMount("/run/user/1000/gvfs/sftp:host=example/home", mount("ext4", "/dev/sda2", "/"), sysfs.root()),
            Classification::Network);
}

TEST(LocationClassifier, RemovableFlagAndMediaMountRoots) {
  const FakeSysfs removable("1");
  EXPECT_EQ(classifyMount("/mnt/usb/photos", mount("vfat", "/dev/sdz1", "/mnt/usb"), removable.root()),
            Classification::Removable);
  EXPECT_EQ(classifyMount("/mnt/usb", mount("vfat", "/dev/sdz", "/mnt/usb"), removable.root()),
            Classification::Removable);
  const FakeSysfs fixed("0");
  EXPECT_EQ(classifyMount("/mnt/data/x", mount("ext4", "/dev/sdz1", "/mnt/data"), fixed.root()), Classification::Local);
  EXPECT_EQ(classifyMount("/run/media/u/usb/x", mount("exfat", "/dev/sdz1", "/run/media/u/usb"), fixed.root()),
            Classification::Removable);
  EXPECT_EQ(classifyMount("/media/usb", mount("exfat", "/dev/sdz1", "/media/usb"), fixed.root()),
            Classification::Removable);
}

TEST(LocationClassifier, EverythingElseIsLocal) {
  const FakeSysfs sysfs("0");
  EXPECT_EQ(classifyMount("/tmp/work", mount("tmpfs", "tmpfs", "/tmp"), sysfs.root()), Classification::Local);
  EXPECT_EQ(classifyMount("/mnt/data", mount("ext4", "/dev/sdz1", "/mnt/data"), sysfs.root()), Classification::Local);
  EXPECT_EQ(classifyMount("/home/u", mount("btrfs", "/dev/nvme0n1p2", "/"), sysfs.root()), Classification::Local);
  // /mediaserver is not /media.
  EXPECT_EQ(classifyMount("/mediaserver", mount("ext4", "/dev/sdz1", "/mediaserver"), sysfs.root()),
            Classification::Local);
}

TEST(LocationClassifier, UnresolvableMountIsNotLocal) {
  const FakeSysfs sysfs("0");
  EXPECT_NE(classifyMount("/somewhere", MountInfo{}, sysfs.root()), Classification::Local);
}

TEST(LocationClassifier, RealClassifierReportsTemporaryDirectoryAsLocal) {
  QTemporaryDir dir(fixturePattern("classify-real"));
  ASSERT_TRUE(dir.isValid());
  const RealLocationClassifier classifier;
  EXPECT_EQ(classifier.classify(dir.path()), Classification::Local);
}
