#include "search_exclusion_policy.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {
QString xdgRoot(const char* variable, const QString& fallback) {
  const QString value = qEnvironmentVariable(variable);
  return QDir::cleanPath(QDir::isAbsolutePath(value) ? value : QDir::homePath() + fallback);
}
bool within(const QString& path, const QString& parent) {
  return path == parent || path.startsWith(parent == QStringLiteral("/") ? parent : parent + u'/');
}
void normalize(QStringList& values) {
  values.removeDuplicates();
  std::ranges::sort(values);
}
}  // namespace

SearchExclusionPolicy SearchExclusionPolicy::compile(const SearchSettings& settings, QStringList& diagnostics) {
  SearchExclusionPolicy policy;
  policy.paths_ = {
      QDir::cleanPath(QDir::homePath() + QStringLiteral("/.cache")),
      xdgRoot("XDG_CACHE_HOME", QStringLiteral("/.cache")),
      QDir::cleanPath(xdgRoot("XDG_DATA_HOME", QStringLiteral("/.local/share")) + QStringLiteral("/Trash")),
  };
  policy.patterns_ = {
      QStringLiteral(".git"),         QStringLiteral(".venv"),       QStringLiteral(".codex"),
      QStringLiteral(".claude"),      QStringLiteral(".agents"),     QStringLiteral("build*"),
      QStringLiteral("node_modules"), QStringLiteral("__pycache__"),
  };
  for (QString path : settings.paths) {
    if (path.startsWith(QStringLiteral("~/"))) {
      path = QDir::homePath() + path.mid(1);
    }
    if (!QDir::isAbsolutePath(path) || path.contains(u'$')) {
      diagnostics.append(
          QCoreApplication::translate("settings", "[search] exclude_paths: invalid path '%1'; ignored").arg(path));
      continue;
    }
    policy.paths_.append(QDir::cleanPath(path));
  }
  for (const QString& pattern : settings.patterns) {
    if (pattern.contains(u'/')) {
      diagnostics.append(
          QCoreApplication::translate("settings", "[search] exclude_directory_patterns: invalid pattern '%1'; ignored")
              .arg(pattern));
      continue;
    }
    policy.patterns_.append(pattern);
  }
  normalize(policy.paths_);
  normalize(policy.patterns_);
  for (const QString& pattern : policy.patterns_) {
    QString expression;
    for (const QChar character : pattern) {
      if (character == u'*') {
        expression += QStringLiteral(".*");
      } else if (character == u'?') {
        expression += u'.';
      } else {
        expression += QRegularExpression::escape(QString(character));
      }
    }
    policy.expressions_.append(QRegularExpression(QRegularExpression::anchoredPattern(expression),
                                                  QRegularExpression::DotMatchesEverythingOption));
  }
  return policy;
}

bool SearchExclusionPolicy::excludes(const QString& path, bool directory, const QString& root) const {
  for (const QString& excluded : paths_) {
    // The selected root grants access through ancestor exclusions only.
    if (!within(root, excluded) && within(path, excluded)) {
      return true;
    }
  }
  if (directory) {
    const QString name = QFileInfo(path).fileName();
    return std::ranges::any_of(expressions_, [&](const auto& expression) { return expression.match(name).hasMatch(); });
  }
  return false;
}

QByteArray SearchExclusionPolicy::fingerprint() const {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << paths_ << patterns_;
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
