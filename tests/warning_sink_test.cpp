#include "warning_sink.h"

#include "directory_fixtures.h"

#include <QFile>
#include <QTemporaryFile>

#include <cstdio>
#include <gtest/gtest.h>
#include <unistd.h>

TEST(WarningSink, StderrSinkWritesExactlyOneLineToStderr) {
  QTemporaryFile capture(files_test::fixturePattern("stderr-capture"));
  ASSERT_TRUE(capture.open());
  std::fflush(stderr);
  const int saved = ::dup(STDERR_FILENO);
  ASSERT_GE(saved, 0);
  ASSERT_GE(::dup2(capture.handle(), STDERR_FILENO), 0);
  {
    StderrWarningSink sink;
    sink.warn(QStringLiteral("/tmp/config.toml:3: unknown key [general] bogus; ignored"));
  }
  std::fflush(stderr);
  ::dup2(saved, STDERR_FILENO);
  ::close(saved);
  QFile written(capture.fileName());
  ASSERT_TRUE(written.open(QIODevice::ReadOnly));
  const auto output = written.readAll();
  EXPECT_EQ(output.count('\n'), 1);
  EXPECT_TRUE(output.endsWith('\n'));
  EXPECT_TRUE(output.contains("/tmp/config.toml:3: unknown key [general] bogus; ignored"));
}
