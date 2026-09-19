#pragma once

#include <QStringList>

#include <optional>

// Internal startup resolution; main retains command-line syntax validation.
struct ResolvedDirectory {
  QString path;
  QString fallback_reason;
};
ResolvedDirectory resolveInitialDirectory(const QStringList& arguments);

// Exactly one of resolved/pending_candidate_path is set. A pending candidate must still be
// validated and classified on the directory worker thread (SPEC.md REQ-F-016), so planning never
// touches the candidate path itself.
struct StartupPlan {
  std::optional<ResolvedDirectory> resolved;
  QString pending_candidate_path;
};
// Precedence: folder argument, then restore (when enabled and a location is stored), then home.
StartupPlan planStartup(const QStringList& arguments, bool restoreEnabled,
                        const std::optional<QString>& storedLocation);
