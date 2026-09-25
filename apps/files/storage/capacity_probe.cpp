#include "capacity_probe.h"

#include <QStorageInfo>

#include <algorithm>

Capacity StorageInfoCapacityProbe::measure(const QString& mountPoint) const {
  const QStorageInfo storage(mountPoint);
  if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() <= 0) {
    return {};
  }
  return {.valid = true,
          .bytesAvailable = static_cast<quint64>(std::max<qint64>(storage.bytesAvailable(), 0)),
          .bytesTotal = static_cast<quint64>(storage.bytesTotal())};
}
