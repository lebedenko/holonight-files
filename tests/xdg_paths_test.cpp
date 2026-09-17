#include "settings/xdg_paths.h"

#include "directory_fixtures.h"

#include <QDir>

#include <gtest/gtest.h>

using files_test::ScopedXdgConfigHome;
using files_test::ScopedXdgDataHome;
using files_test::ScopedXdgStateHome;

TEST(XdgPaths, AbsoluteVariablesAreUsed) {
  const ScopedXdgConfigHome config(QStringLiteral("/tmp/hn-config"));
  const ScopedXdgStateHome state(QStringLiteral("/tmp/hn-state/"));
  const ScopedXdgDataHome data(QStringLiteral("/tmp/hn-data"));
  EXPECT_EQ(XdgPaths::configFilePath(), "/tmp/hn-config/holonight-files/config.toml");
  EXPECT_EQ(XdgPaths::stateDirPath(), "/tmp/hn-state/holonight-files");
  EXPECT_EQ(XdgPaths::stateFilePath(), "/tmp/hn-state/holonight-files/state.toml");
  EXPECT_EQ(XdgPaths::userDirsFilePath(), "/tmp/hn-config/user-dirs.dirs");
  EXPECT_EQ(XdgPaths::dataDirPath(), "/tmp/hn-data/holonight/holonight-files");
  EXPECT_EQ(XdgPaths::placesFilePath(), "/tmp/hn-data/holonight/holonight-files/places.toml");
}

TEST(XdgPaths, UnsetEmptyAndRelativeVariablesFallBackToHome) {
  const auto home = QDir::homePath();
  for (const std::optional<QString>& value :
       {std::optional<QString>{}, std::optional<QString>{QString{}}, std::optional<QString>{"relative/dir"}}) {
    const ScopedXdgConfigHome config(value);
    const ScopedXdgStateHome state(value);
    const ScopedXdgDataHome data(value);
    EXPECT_EQ(XdgPaths::configFilePath(), home + "/.config/holonight-files/config.toml");
    EXPECT_EQ(XdgPaths::stateFilePath(), home + "/.local/state/holonight-files/state.toml");
    EXPECT_EQ(XdgPaths::userDirsFilePath(), home + "/.config/user-dirs.dirs");
    EXPECT_EQ(XdgPaths::dataDirPath(), home + "/.local/share/holonight/holonight-files");
    EXPECT_EQ(XdgPaths::placesFilePath(), home + "/.local/share/holonight/holonight-files/places.toml");
  }
}
