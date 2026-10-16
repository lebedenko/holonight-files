#pragma once
#include <HolonightFileBrowser/search/path_index_store.h>
class PathIndexStore : public BrowserPathIndexStore {
 public:
  using BrowserPathIndexStore::BrowserPathIndexStore;
  static QString cachePath();
};
