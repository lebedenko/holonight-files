#include "restore_outcome.h"

#include <QCoreApplication>

QString restoreOutcomeReason(RestoreOutcome outcome) {
  switch (outcome) {
    case RestoreOutcome::Ok:
      break;
    case RestoreOutcome::DoesNotExist:
      return QCoreApplication::translate("main", "last location does not exist");
    case RestoreOutcome::NotDirectory:
      return QCoreApplication::translate("main", "last location is not a directory");
    case RestoreOutcome::NotReadable:
      return QCoreApplication::translate("main", "last location is not readable");
    case RestoreOutcome::NotLocal:
      return QCoreApplication::translate("main", "last location is not local");
  }
  return {};
}
