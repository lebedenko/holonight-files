#pragma once

#include <QStringList>

// Internal startup resolution; main retains command-line syntax validation.
struct ResolvedDirectory {
  QString path;
  QString fallback_reason;
};
ResolvedDirectory resolveInitialDirectory(const QStringList& arguments);
