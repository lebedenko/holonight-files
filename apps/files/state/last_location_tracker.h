#pragma once

#include "location_classifier.h"

#include <QString>

// The session's most recent folder that loaded successfully and is Local (SPEC.md REQ-F-019):
// Network and Removable loads never replace it (REQ-F-026).
class LastLocationTracker {
 public:
  void recordLoad(const QString& path, LocationClassifier::Classification classification);
  bool hasCandidate() const { return !candidate_.isEmpty(); }
  QString candidate() const { return candidate_; }

 private:
  QString candidate_;
};
