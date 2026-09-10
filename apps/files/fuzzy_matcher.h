#pragma once

#include <QList>
#include <QString>

// fzf-style fuzzy scoring for SEARCH mode (SPEC.md REQ-F-025): rewards contiguous runs and
// matches starting at a word boundary, penalizes gaps between matched characters. A free,
// value-semantics function (not a QObject) so it's unit-testable without VimModeController or
// any QML — matches candidate as a case-insensitive subsequence of query, not a substring.
struct FuzzyMatch {
  bool matched = false;
  int score = 0;
  QList<int> positions;  // candidate-string indices of matched characters, in ascending order
};

FuzzyMatch fuzzyMatch(const QString& query, const QString& candidate);
