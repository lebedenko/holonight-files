#pragma once

#include <QString>

#include <optional>

class WarningSink;

struct StateLoadResult {
  // Empty means "no stored location" (SPEC.md REQ-F-008).
  std::optional<QString> last_location;
};

// state.toml I/O. Reads are silent for a missing file and warn once for anything unusable
// (REQ-F-008); writes are atomic through QSaveFile only (REQ-C-004). No locking between instances:
// the last commit wins (REQ-F-021, REQ-C-008).
class StateStore {
 public:
  static constexpr int kVersion = 1;
  StateStore(QString stateFilePath, QString stateDirPath);
  StateLoadResult load(WarningSink& warnings) const;
  // Returns false after emitting one warning; never blocks or prompts (REQ-F-010).
  bool save(const QString& lastLocation, WarningSink& warnings) const;

 private:
  QString file_path_;
  QString dir_path_;
};
