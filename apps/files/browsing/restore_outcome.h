#pragma once

#include <QString>

// Result of validating a stored last location on the worker thread (SPEC.md REQ-F-013/016).
enum class RestoreOutcome { Ok, DoesNotExist, NotDirectory, NotReadable, NotLocal };

// The status-bar fallback reason for a failed restore (REQ-F-014); empty for Ok.
QString restoreOutcomeReason(RestoreOutcome outcome);
