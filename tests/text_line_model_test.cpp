#include "text_line_model.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

namespace {

QVariant roleValue(const TextLineModel& model, int row, TextLineModel::Role role) {
  return model.data(model.index(row), role);
}

}  // namespace

TEST(TextLineModel, StartsEmpty) {
  TextLineModel model;
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(TextLineModel, ExposesNamedRoles) {
  TextLineModel model;
  const auto names = model.roleNames();
  EXPECT_EQ(names.value(TextLineModel::LineTextRole), QByteArrayLiteral("lineText"));
  EXPECT_EQ(names.value(TextLineModel::LineNumberRole), QByteArrayLiteral("lineNumber"));
}

TEST(TextLineModel, SetLinesMatchesRowCountAndRoleData) {
  TextLineModel model;
  model.setLines({QStringLiteral("alpha"), QString(), QStringLiteral("日本語")});
  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(roleValue(model, 0, TextLineModel::LineTextRole).toString(), QStringLiteral("alpha"));
  EXPECT_EQ(roleValue(model, 1, TextLineModel::LineTextRole).toString(), QString());
  EXPECT_EQ(roleValue(model, 2, TextLineModel::LineTextRole).toString(), QStringLiteral("日本語"));
  EXPECT_EQ(roleValue(model, 0, TextLineModel::LineNumberRole).toInt(), 1);
  EXPECT_EQ(roleValue(model, 2, TextLineModel::LineNumberRole).toInt(), 3);
}

TEST(TextLineModel, SetLinesReplacesPreviousLinesWithOneReset) {
  TextLineModel model;
  model.setLines({QStringLiteral("a"), QStringLiteral("b")});
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  model.setLines({QStringLiteral("only")});
  EXPECT_EQ(reset.count(), 1);
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(roleValue(model, 0, TextLineModel::LineTextRole).toString(), QStringLiteral("only"));
}

TEST(TextLineModel, ClearEmptiesTheModelAndResets) {
  TextLineModel model;
  model.setLines({QStringLiteral("a")});
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  model.clear();
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(reset.count(), 1);
}

TEST(TextLineModel, ClearOnAnEmptyModelDoesNotReset) {
  TextLineModel model;
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  model.clear();
  EXPECT_EQ(reset.count(), 0);
}

TEST(TextLineModel, InvalidIndexAndUnknownRoleYieldNoData) {
  TextLineModel model;
  model.setLines({QStringLiteral("a")});
  EXPECT_FALSE(model.data(model.index(0), Qt::DisplayRole).isValid());
  EXPECT_EQ(model.rowCount(model.index(0)), 0);  // a flat list has no children
}
