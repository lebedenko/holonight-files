#include "state/last_location_tracker.h"

void LastLocationTracker::recordLoad(const QString& path, LocationClassifier::Classification classification) {
  if (classification == LocationClassifier::Classification::Local && !path.isEmpty()) {
    candidate_ = path;
  }
}
