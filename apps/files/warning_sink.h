#pragma once

#include <QString>

// The single channel for configuration/state warnings (SPEC.md REQ-C-011). Abstract so tests can
// record warnings instead of parsing captured stderr; production always uses StderrWarningSink.
class WarningSink {
 public:
  WarningSink() = default;
  virtual ~WarningSink() = default;
  WarningSink(const WarningSink&) = delete;
  WarningSink& operator=(const WarningSink&) = delete;
  WarningSink(WarningSink&&) = delete;
  WarningSink& operator=(WarningSink&&) = delete;
  // message is one line, without a trailing newline.
  virtual void warn(const QString& message) = 0;
};

class StderrWarningSink : public WarningSink {
 public:
  void warn(const QString& message) override;
};
