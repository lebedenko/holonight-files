// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HolonightFileBrowser/preview/runtime_options.h>
#include <cstring>
#include <span>
namespace files_test {
inline HolonightFileBrowser::ImageRuntimeOptions imageRuntime = HolonightFileBrowser::defaultImageRuntimeOptions();
inline void readImageRuntime(int& argc, char** argv) {
  auto arguments = std::span(argv, static_cast<std::size_t>(argc) + 1);
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(arguments[i], "--image-runtime") == 0) {
      imageRuntime.directory = QString::fromLocal8Bit(arguments[i + 1]);
      for (int j = i; j + 2 <= argc; ++j) {
        arguments[j] = arguments[j + 2];
      }
      argc -= 2;
      break;
    }
  }
}
}  // namespace files_test
