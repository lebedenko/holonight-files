#include "fuzzy_matcher.h"

#include <QVector>

#include <limits>

namespace {
constexpr int kConsecutiveBonus = 15;
constexpr int kWordBoundaryBonus = 10;
constexpr int kGapPenaltyPerChar = 2;
constexpr int kBaseScore = 1;
constexpr int kNegInf = std::numeric_limits<int>::min() / 2;

bool isSeparator(QChar character) {
  return character == u'.' || character == u'_' || character == u'-' || character == u' ';
}

// Cheap subsequence existence pre-filter (case-insensitive): most keystrokes against most rows
// don't match at all, so bail before the O(query·candidate^2) scoring DP runs (see
// docs/sdd/vim-modal-editing/DESIGN.md Known Risks re: fuzzy-match cost at scale).
bool isSubsequence(const QString& foldedQuery, const QString& foldedCandidate) {
  int queryIndex = 0;
  for (int candidateIndex = 0; candidateIndex < foldedCandidate.size() && queryIndex < foldedQuery.size();
       ++candidateIndex) {
    if (foldedCandidate.at(candidateIndex) == foldedQuery.at(queryIndex)) {
      ++queryIndex;
    }
  }
  return queryIndex == foldedQuery.size();
}

// Fills row (dpAll[0]): best score for matching just the first query character, for every
// candidate position it could align to.
void seedFirstRow(const QString& foldedQuery, const QString& foldedCandidate, const QString& candidate,
                  QVector<int>& row) {
  for (int j = 0; j < foldedCandidate.size(); ++j) {
    if (foldedQuery.at(0) != foldedCandidate.at(j)) {
      continue;
    }
    int score = kBaseScore;
    if (j == 0 || isSeparator(candidate.at(j - 1))) {
      score += kWordBoundaryBonus;
    }
    row[j] = score;
  }
}

// Fills row (dpAll[queryIndex]) from previousRow (dpAll[queryIndex - 1]), recording each winning
// predecessor in parentRow so the caller can reconstruct the matched positions afterward.
void extendRow(const QString& foldedQuery, const QString& foldedCandidate, const QString& candidate, int queryIndex,
               const QVector<int>& previousRow, QVector<int>& row, QVector<int>& parentRow) {
  for (int j = queryIndex; j < foldedCandidate.size(); ++j) {
    if (foldedQuery.at(queryIndex) != foldedCandidate.at(j)) {
      continue;
    }
    int best = kNegInf;
    int bestPredecessor = -1;
    for (int k = queryIndex - 1; k < j; ++k) {
      if (previousRow[k] <= kNegInf) {
        continue;
      }
      const int gap = j - k - 1;
      int score = previousRow[k] + kBaseScore - (gap * kGapPenaltyPerChar);
      if (gap == 0) {
        score += kConsecutiveBonus;
      }
      if (isSeparator(candidate.at(j - 1))) {
        score += kWordBoundaryBonus;
      }
      if (score > best) {
        best = score;
        bestPredecessor = k;
      }
    }
    if (best > kNegInf) {
      row[j] = best;
      parentRow[j] = bestPredecessor;
    }
  }
}

QList<int> reconstructPositions(const QVector<QVector<int>>& parent, int queryLength, int bestEnd) {
  QList<int> positions;
  int candidateIndex = bestEnd;
  for (int i = queryLength - 1; i >= 0; --i) {
    positions.prepend(candidateIndex);
    candidateIndex = parent[i][candidateIndex];
  }
  return positions;
}
}  // namespace

FuzzyMatch fuzzyMatch(const QString& query, const QString& candidate) {
  FuzzyMatch result;
  const int queryLength = static_cast<int>(query.size());
  const int candidateLength = static_cast<int>(candidate.size());
  if (queryLength == 0 || candidateLength == 0 || queryLength > candidateLength) {
    return result;
  }

  const QString foldedQuery = query.toCaseFolded();
  const QString foldedCandidate = candidate.toCaseFolded();
  if (!isSubsequence(foldedQuery, foldedCandidate)) {
    return result;
  }

  // dpAll[i][j]: best score matching the first (i+1) query characters, with the (i+1)-th query
  // character matched at candidate index j. parent[i][j]: the candidate index the previous query
  // character matched at, to reconstruct the winning position list.
  QVector<QVector<int>> dpAll(queryLength, QVector<int>(candidateLength, kNegInf));
  QVector<QVector<int>> parent(queryLength, QVector<int>(candidateLength, -1));

  seedFirstRow(foldedQuery, foldedCandidate, candidate, dpAll[0]);
  for (int i = 1; i < queryLength; ++i) {
    extendRow(foldedQuery, foldedCandidate, candidate, i, dpAll[i - 1], dpAll[i], parent[i]);
  }

  int bestEnd = -1;
  int bestScore = kNegInf;
  for (int j = queryLength - 1; j < candidateLength; ++j) {
    if (dpAll[queryLength - 1][j] > bestScore) {
      bestScore = dpAll[queryLength - 1][j];
      bestEnd = j;
    }
  }
  if (bestEnd < 0) {
    return result;  // subsequence exists but every alignment scored at -inf: unreachable in practice
  }

  result.matched = true;
  result.score = bestScore;
  result.positions = reconstructPositions(parent, queryLength, bestEnd);
  return result;
}
