#pragma once
#include <HolonightFileBrowser/search/path_finder_model.h>
// Files owns persistence and reloads its settings every time the finder opens.
class PathFinderModel : public BrowserPathFinderModel {
  Q_OBJECT
 public:
  explicit PathFinderModel(QObject* parent = nullptr);
  explicit PathFinderModel(PathScanFunction scanner, QObject* parent = nullptr);

 private:
  void configure();
};
