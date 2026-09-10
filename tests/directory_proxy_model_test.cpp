#include "directory_proxy_model.h"

#include "directory_fixtures.h"
#include "directory_model.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;

namespace {
bool settled(const DirectoryModel& model) {
  return QTest::qWaitFor([&] { return !model.scanning(); });
}
QStringList proxyNames(const DirectoryProxyModel& proxy) {
  QStringList names;
  for (int row = 0; row < proxy.rowCount(); ++row) {
    names.append(proxy.data(proxy.index(row, 0), DirectoryModel::NameRole).toString());
  }
  return names;
}
}  // namespace

TEST(DirectoryProxyModel, SortsDirectoriesBeforeFilesWithNaturalCaseInsensitiveOrder) {
  QTemporaryDir dir(fixturePattern("proxy-sort"));
  ASSERT_TRUE(dir.isValid());
  // Numeric values kept distinct (1 < 2 < 10) to avoid depending on unspecified tie-break
  // behavior between collator-equal keys like "file2" vs "file02" — that nuance is a QCollator
  // implementation detail, not something DirectoryProxyModel itself is responsible for.
  for (const auto& name : {"File10.txt", "file2.txt", "File1.txt", "Banana.txt"}) {
    ASSERT_FALSE(writeFile(dir, QString::fromUtf8(name)).isEmpty());
  }
  ASSERT_TRUE(QDir(dir.path()).mkdir("zzz-folder"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("aaa-folder"));
  const auto originalLocale = QLocale();
  DirectoryModel model;
  DirectoryProxyModel proxy;
  EXPECT_EQ(QLocale(), originalLocale);
  proxy.setSourceModel(&model);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  const QStringList expected{"aaa-folder", "zzz-folder", "Banana.txt", "File1.txt", "file2.txt", "File10.txt"};
  EXPECT_EQ(proxyNames(proxy), expected);
  proxy.setSortDescending(true);
  auto reversed = expected;
  std::ranges::reverse(reversed);
  EXPECT_EQ(proxyNames(proxy), reversed);
}

TEST(DirectoryProxyModel, HiddenFilesAreExcludedByDefaultAndToggleImmediately) {
  QTemporaryDir dir(fixturePattern("proxy-hidden"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, ".hidden.txt").isEmpty());
  ASSERT_FALSE(writeFile(dir, "visible.txt").isEmpty());
  DirectoryModel model;
  DirectoryProxyModel proxy;
  proxy.setSourceModel(&model);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(proxy.hiddenVisible());
  EXPECT_EQ(proxyNames(proxy), (QStringList{"visible.txt"}));
  proxy.setHiddenVisible(true);
  EXPECT_EQ(proxyNames(proxy), (QStringList{".hidden.txt", "visible.txt"}));
  proxy.setHiddenVisible(false);
  EXPECT_EQ(proxyNames(proxy), (QStringList{"visible.txt"}));
}

TEST(DirectoryProxyModel, SortDirectionTogglesWithoutFullModelReset) {
  QTemporaryDir dir(fixturePattern("proxy-order"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "a.txt").isEmpty());
  ASSERT_FALSE(writeFile(dir, "b.txt").isEmpty());
  ASSERT_FALSE(writeFile(dir, "c.txt").isEmpty());
  DirectoryModel model;
  DirectoryProxyModel proxy;
  proxy.setSourceModel(&model);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(proxy.sortDescending());
  EXPECT_EQ(proxyNames(proxy), (QStringList{"a.txt", "b.txt", "c.txt"}));
  QSignalSpy reset(&proxy, &QAbstractItemModel::modelReset);
  proxy.setSortDescending(true);
  EXPECT_TRUE(proxy.sortDescending());
  EXPECT_EQ(proxyNames(proxy), (QStringList{"c.txt", "b.txt", "a.txt"}));
  EXPECT_EQ(reset.count(), 0);
  proxy.setSortDescending(false);
  EXPECT_EQ(proxyNames(proxy), (QStringList{"a.txt", "b.txt", "c.txt"}));
}

TEST(DirectoryProxyModel, PlaceholderAdjacentToExactAnchorAcrossDirectionsGroupsAndTies) {
  QTemporaryDir dir(fixturePattern("placeholder-order"));
  for (const auto& name : {"A", "a", "file02", "file2", "z"}) {
    writeFile(dir, name);
  }
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  DirectoryModel model;
  DirectoryProxyModel proxy;
  proxy.setSourceModel(&model);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  for (bool descending : {false, true}) {
    proxy.setSortDescending(descending);
    const auto original = proxyNames(proxy);
    for (int anchor = 0; anchor < original.size(); ++anchor) {
      for (bool below : {false, true}) {
        const int sourceRow = model.rowCount();
        const bool isDir = proxy.data(proxy.index(anchor, 0), DirectoryModel::IsDirRole).toBool();
        proxy.setPlaceholder(sourceRow, original[anchor], isDir, below);
        model.insertPlaceholderRow();
        auto expected = original;
        expected.insert(anchor + (below ? 1 : 0), QString{});
        EXPECT_EQ(proxyNames(proxy), expected);
        model.removePlaceholderRow(sourceRow);
        proxy.setPlaceholder(-1, {}, false, false);
      }
    }
  }
}

TEST(DirectoryProxyModel, EmptyPlaceholderAndEditingLocks) {
  DirectoryModel model;
  DirectoryProxyModel proxy;
  proxy.setSourceModel(&model);
  for (bool descending : {false, true}) {
    proxy.setSortDescending(descending);
    proxy.setPlaceholder(0, {}, false, true);
    model.insertPlaceholderRow();
    EXPECT_EQ(proxy.rowCount(), 1);
    model.removePlaceholderRow(0);
    proxy.setPlaceholder(-1, {}, false, false);
  }
  proxy.setEditing(true);
  proxy.setSortDescending(false);
  proxy.setHiddenVisible(true);
  EXPECT_TRUE(proxy.sortDescending());
  EXPECT_FALSE(proxy.hiddenVisible());
}
