#pragma once

#include <QString>

// Injectable directory-availability check (SPEC.md REQ-F-018/020/021, REQ-NF-001/002), mirroring
// the existing LocationClassifier seam (location_classifier.h) used by DirectoryModel::
// validateForRestore. Always called off the GUI thread by PlacesModel.
class PlaceAvailabilityChecker {
 public:
  virtual ~PlaceAvailabilityChecker() = default;
  PlaceAvailabilityChecker() = default;
  PlaceAvailabilityChecker(const PlaceAvailabilityChecker&) = delete;
  PlaceAvailabilityChecker& operator=(const PlaceAvailabilityChecker&) = delete;
  PlaceAvailabilityChecker(PlaceAvailabilityChecker&&) = delete;
  PlaceAvailabilityChecker& operator=(PlaceAvailabilityChecker&&) = delete;

  // True iff path exists, is a directory, and is readable (stat() + access(), or QFileInfo
  // equivalent). May block indefinitely (dead NFS/sshfs) -- callers must never invoke this on the
  // GUI thread (REQ-NF-001/002).
  [[nodiscard]] virtual bool isAvailable(const QString& path) const = 0;
};

class StatPlaceAvailabilityChecker final : public PlaceAvailabilityChecker {
 public:
  [[nodiscard]] bool isAvailable(const QString& path) const override;
};
