#include "state/last_location_tracker.h"

#include <gtest/gtest.h>

using Classification = LocationClassifier::Classification;

TEST(LastLocationTracker, StartsEmpty) {
  const LastLocationTracker tracker;
  EXPECT_FALSE(tracker.hasCandidate());
  EXPECT_TRUE(tracker.candidate().isEmpty());
}

TEST(LastLocationTracker, KeepsTheMostRecentLocalLoad) {
  LastLocationTracker tracker;
  tracker.recordLoad("/a", Classification::Local);
  tracker.recordLoad("/b", Classification::Local);
  tracker.recordLoad("/net", Classification::Network);
  tracker.recordLoad("/usb", Classification::Removable);
  EXPECT_TRUE(tracker.hasCandidate());
  EXPECT_EQ(tracker.candidate(), "/b");
}

TEST(LastLocationTracker, StaysEmptyWhenNothingLocalLoads) {
  LastLocationTracker tracker;
  tracker.recordLoad("/net", Classification::Network);
  tracker.recordLoad("/usb", Classification::Removable);
  EXPECT_FALSE(tracker.hasCandidate());
}
