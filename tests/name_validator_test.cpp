#include "name_validator.h"

#include "directory_fixtures.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;

TEST(NameValidator, AcceptsAnOrdinaryName) {
  QTemporaryDir dir(fixturePattern("validate-ok"));
  ASSERT_TRUE(dir.isValid());
  const auto result = validateName("newfile.txt", dir.path());
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.normalizedName, "newfile.txt");
  EXPECT_FALSE(result.createsDirectory);
  EXPECT_TRUE(result.errorMessage.isEmpty());
}

TEST(NameValidator, StripsALeadingDotSlashSilently) {
  QTemporaryDir dir(fixturePattern("validate-dotslash"));
  ASSERT_TRUE(dir.isValid());
  const auto result = validateName("./example.txt", dir.path());
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.normalizedName, "example.txt");
  EXPECT_TRUE(result.errorMessage.isEmpty());
}

TEST(NameValidator, RejectsLeadingParentTraversal) {
  QTemporaryDir dir(fixturePattern("validate-parent-leading"));
  ASSERT_TRUE(dir.isValid());
  const auto result = validateName("../danger", dir.path());
  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(NameValidator, RejectsEmbeddedParentTraversal) {
  QTemporaryDir dir(fixturePattern("validate-parent-embedded"));
  ASSERT_TRUE(dir.isValid());
  const auto result = validateName("foo/../bar", dir.path());
  EXPECT_FALSE(result.valid);
}

TEST(NameValidator, AcceptsATrailingSlashAsADirectoryMarker) {
  QTemporaryDir dir(fixturePattern("validate-trailing-slash"));
  ASSERT_TRUE(dir.isValid());
  const auto result = validateName("newfolder/", dir.path());
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.normalizedName, "newfolder");
  EXPECT_TRUE(result.createsDirectory);
}

TEST(NameValidator, RejectsAnEmbeddedSlashThatIsNotASingleTrailingMarker) {
  QTemporaryDir dir(fixturePattern("validate-embedded-slash"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_FALSE(validateName("foo/bar", dir.path()).valid);
  EXPECT_FALSE(validateName("foo/bar/", dir.path()).valid);
  EXPECT_FALSE(validateName("foo//", dir.path()).valid);
}

TEST(NameValidator, RejectsEmptyOrWhitespaceOnlyNames) {
  QTemporaryDir dir(fixturePattern("validate-empty"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_FALSE(validateName("", dir.path()).valid);
  EXPECT_FALSE(validateName("   ", dir.path()).valid);
  EXPECT_FALSE(validateName("\t", dir.path()).valid);
}

TEST(NameValidator, RejectsLiteralDotAndDotDot) {
  QTemporaryDir dir(fixturePattern("validate-reserved"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_FALSE(validateName(".", dir.path()).valid);
  EXPECT_FALSE(validateName("..", dir.path()).valid);
}

TEST(NameValidator, RejectsNamesLongerThan255Bytes) {
  QTemporaryDir dir(fixturePattern("validate-toolong"));
  ASSERT_TRUE(dir.isValid());
  const QString longName(256, QLatin1Char('a'));
  EXPECT_FALSE(validateName(longName, dir.path()).valid);
  const QString maxName(255, QLatin1Char('a'));
  EXPECT_TRUE(validateName(maxName, dir.path()).valid);
}

TEST(NameValidator, RejectsControlBytes) {
  QTemporaryDir dir(fixturePattern("validate-control"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_FALSE(validateName(QStringLiteral("bad") + QChar(0x01) + "name", dir.path()).valid);
  EXPECT_FALSE(validateName(QStringLiteral("bad") + QChar(0x7F) + "name", dir.path()).valid);
}

TEST(NameValidator, RejectsCollisionWithAnExistingEntry) {
  QTemporaryDir dir(fixturePattern("validate-collision"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "existing.txt");
  EXPECT_FALSE(validateName("existing.txt", dir.path()).valid);
}

TEST(NameValidator, AcceptsTheUnchangedNameAsATouchNotACollision) {
  QTemporaryDir dir(fixturePattern("validate-self"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "self.txt");
  const auto result = validateName("self.txt", dir.path(), "self.txt");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.normalizedName, "self.txt");
}

TEST(NameValidator, RenameToADifferentExistingNameStillCollides) {
  QTemporaryDir dir(fixturePattern("validate-rename-collision"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "self.txt");
  writeFile(dir, "other.txt");
  EXPECT_FALSE(validateName("other.txt", dir.path(), "self.txt").valid);
}
