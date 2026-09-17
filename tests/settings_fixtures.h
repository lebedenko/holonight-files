#pragma once

#include "location_classifier.h"
#include "warning_sink.h"

#include <QHash>
#include <QSemaphore>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace files_test {

// Collects warnings instead of writing them to stderr.
class RecordingWarningSink : public WarningSink {
 public:
  void warn(const QString& message) override { messages.append(message); }
  QStringList messages;
};

// Holds the first classification until the test releases it; later loads proceed normally.
class GatedLocationClassifier : public LocationClassifier {
 public:
  Classification classify(const QString&) const override {
    if (calls.fetch_add(1) == 0) {
      entered = true;
      release.acquire();
    }
    return Classification::Local;
  }
  mutable std::atomic_bool entered = false;
  mutable std::atomic_int calls = 0;
  mutable QSemaphore release;
};

// Returns a fixed classification, recording which thread asked and optionally stalling like a
// hung mount would.
class FakeLocationClassifier : public LocationClassifier {
 public:
  explicit FakeLocationClassifier(Classification result, std::chrono::milliseconds delay = {})
      : result_(result), delay_(delay) {}
  Classification classify(const QString& path) const override {
    {
      const std::scoped_lock lock(mutex_);
      threads_.push_back(QThread::currentThread());
      paths_.append(path);
    }
    if (delay_.count() > 0) {
      std::this_thread::sleep_for(delay_);
    }
    return overrides.value(path, result_);
  }
  // Per-path results; fill in before handing the classifier to a model.
  QHash<QString, Classification> overrides;
  std::vector<QThread*> threads() const {
    const std::scoped_lock lock(mutex_);
    return threads_;
  }
  QStringList paths() const {
    const std::scoped_lock lock(mutex_);
    return paths_;
  }

 private:
  Classification result_;
  std::chrono::milliseconds delay_;
  mutable std::mutex mutex_;
  mutable std::vector<QThread*> threads_;
  mutable QStringList paths_;
};

}  // namespace files_test
