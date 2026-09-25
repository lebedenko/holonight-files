#pragma once
#include <QString>

// Filesystem capacity at one mount point. Both figures come from one query, so a bar and its text
// can never disagree (REQ-F-041). valid == false covers unmounted, failed and not-yet-measured alike.
struct Capacity {
  bool valid = false;
  quint64 bytesAvailable = 0;  // Space available to an unprivileged user (REQ-F-039).
  quint64 bytesTotal = 0;
  bool operator==(const Capacity&) const = default;
};

// Replaceable so tests never read real filesystem capacity (REQ-C-006). measure() stats a mount point
// and may block on an unresponsive device, so it is only ever called off the GUI thread (REQ-F-042).
class CapacityProbe {
 public:
  CapacityProbe() = default;
  virtual ~CapacityProbe() = default;
  CapacityProbe(const CapacityProbe&) = delete;
  CapacityProbe& operator=(const CapacityProbe&) = delete;
  CapacityProbe(CapacityProbe&&) = delete;
  CapacityProbe& operator=(CapacityProbe&&) = delete;
  [[nodiscard]] virtual Capacity measure(const QString& mountPoint) const = 0;
};

// QStorageInfo::bytesAvailable(), not bytesFree(): the latter includes root-reserved blocks.
class StorageInfoCapacityProbe : public CapacityProbe {
 public:
  [[nodiscard]] Capacity measure(const QString& mountPoint) const override;
};
