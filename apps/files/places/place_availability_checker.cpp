#include "places/place_availability_checker.h"

#include <QFileInfo>

bool StatPlaceAvailabilityChecker::isAvailable(const QString& path) const {
  const QFileInfo info(path);
  return info.isDir() && info.isReadable();
}
