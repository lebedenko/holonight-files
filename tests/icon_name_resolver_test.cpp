#include "icon_name_resolver.h"

#include <QSet>

#include <gtest/gtest.h>
#include <sys/stat.h>

// SPEC.md REQ-F-006. MIME chains are pinned against shared-mime-info 2.5; a database update that
// changes a type's declared parents shows up here first rather than as a silently different icon.

TEST(IconNameResolver, DirectoryPrefersFolderThenInodeDirectory) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFDIR | 0755, "photos.jpg"),
            (QStringList{"folder", "inode-directory"}));
}

TEST(IconNameResolver, FifoUsesModeOnly) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFIFO | 0600, "pipe.txt"), (QStringList{"inode-fifo"}));
}

TEST(IconNameResolver, SocketUsesModeOnly) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFSOCK | 0600, "bus.py"), (QStringList{"inode-socket"}));
}

TEST(IconNameResolver, CharacterDeviceUsesModeOnly) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFCHR | 0600, "tty0"), (QStringList{"inode-chardevice"}));
}

TEST(IconNameResolver, BlockDeviceUsesModeOnly) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFBLK | 0600, "sda"), (QStringList{"inode-blockdevice"}));
}

TEST(IconNameResolver, PythonFileWalksOwnNamesThenParentsInOrder) {
  // text/x-python declares application/x-executable and text/x-cython as parents; text/plain is
  // reached through text/x-cython. application/octet-stream is skipped — its generic icon is the
  // final fallback, which would otherwise precede text-plain.
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFREG | 0644, "script.py"),
            (QStringList{"text-x-python", "text-x-generic", "application-x-executable", "text-x-cython", "text-plain",
                         "application-x-generic"}));
}

TEST(IconNameResolver, JpegFile) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFREG | 0644, "photo.jpg"),
            (QStringList{"image-jpeg", "image-x-generic", "application-x-generic"}));
}

TEST(IconNameResolver, ExtensionlessReadmeMatchesByName) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFREG | 0644, "README"),
            (QStringList{"text-x-readme", "text-x-generic", "text-plain", "application-x-generic"}));
}

TEST(IconNameResolver, UnregisteredExtensionIsGenericOnly) {
  EXPECT_EQ(IconNameResolver::candidateIconNames(S_IFREG | 0644, "data.xyz123"),
            (QStringList{"application-x-generic"}));
}

TEST(IconNameResolver, ChainNeverRepeatsANameAndNeverContainsTheSeparator) {
  for (const auto* name : {"script.py", "photo.jpg", "README", "Makefile", "doc.pdf", "notes.txt", "archive.tar.gz"}) {
    const auto chain = IconNameResolver::candidateIconNames(S_IFREG | 0644, QString::fromUtf8(name));
    EXPECT_EQ(chain.size(), QSet<QString>(chain.begin(), chain.end()).size()) << name;
    EXPECT_EQ(chain.last(), "application-x-generic") << name;
    for (const auto& candidate : chain) {
      EXPECT_FALSE(candidate.contains(IconNameResolver::kChainSeparator)) << name;
    }
  }
}

TEST(IconNameResolver, GenericFallbackNames) {
  EXPECT_EQ(IconNameResolver::genericFallbackName(false), "application-x-generic");
  EXPECT_EQ(IconNameResolver::genericFallbackName(true), "folder");
}
