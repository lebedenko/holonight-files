#include "clipboard_register.h"

#include <gtest/gtest.h>

TEST(ClipboardRegister, StoresPathsAndCutFlag) {
  ClipboardRegister reg;
  reg.paths = {"/a", "/b"};
  reg.cut = true;
  EXPECT_EQ(reg.paths, (QStringList{"/a", "/b"}));
  EXPECT_TRUE(reg.cut);
}

TEST(ClipboardRegister, OverwriteSilentlyReplacesContents) {
  ClipboardRegister reg;
  reg.paths = {"/a"};
  reg.cut = false;
  reg.paths = {"/b", "/c"};
  reg.cut = true;
  EXPECT_EQ(reg.paths, (QStringList{"/b", "/c"}));
  EXPECT_TRUE(reg.cut);
}

TEST(ClipboardRegister, ClearResetsState) {
  ClipboardRegister reg;
  reg.paths = {"/a"};
  reg.cut = true;
  reg.clear();
  EXPECT_TRUE(reg.paths.isEmpty());
  EXPECT_FALSE(reg.cut);
}
