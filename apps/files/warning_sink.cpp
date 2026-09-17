#include "warning_sink.h"

#include <QTextStream>

void StderrWarningSink::warn(const QString& message) {
  // Same mechanism main.cpp uses for its own command-line diagnostics.
  QTextStream(stderr) << QStringLiteral("hn-files: warning: ") << message << '\n';
}
