#include "fuzzy_matcher.h"

#include <gtest/gtest.h>

TEST(FuzzyMatcher, MatchesASimpleSubsequence) {
  const auto match = fuzzyMatch("ex", "example.txt");
  EXPECT_TRUE(match.matched);
  EXPECT_EQ(match.positions, (QList<int>{0, 1}));
}

TEST(FuzzyMatcher, DoesNotMatchWhenNotASubsequence) {
  const auto match = fuzzyMatch("xyz", "example.txt");
  EXPECT_FALSE(match.matched);
}

TEST(FuzzyMatcher, IsCaseInsensitive) {
  EXPECT_TRUE(fuzzyMatch("EX", "example.txt").matched);
  EXPECT_TRUE(fuzzyMatch("ex", "EXAMPLE.TXT").matched);
}

TEST(FuzzyMatcher, PrefersContiguousRunsOverScatteredMatches) {
  // "ex" as a contiguous run in "example" should outscore the same two letters scattered across
  // "e-x-tra-file" (fzf-style consecutive-run bonus).
  const auto contiguous = fuzzyMatch("ex", "example.txt");
  const auto scattered = fuzzyMatch("ex", "extra-file.txt");  // still contiguous here; use a truly scattered candidate
  const auto reallyScattered = fuzzyMatch("ex", "e_x_tra.txt");
  ASSERT_TRUE(contiguous.matched);
  ASSERT_TRUE(reallyScattered.matched);
  EXPECT_GT(contiguous.score, reallyScattered.score);
  EXPECT_TRUE(scattered.matched);
}

TEST(FuzzyMatcher, PrefersAWordBoundaryStartOverAMidWordMatch) {
  // "doc" matches at the start of "doc-final.txt" (word boundary) and also mid-word inside
  // "my-doc.txt" preceded by a separator — both are boundary matches; compare against a
  // non-boundary occurrence.
  const auto boundary = fuzzyMatch("doc", "doc.txt");
  const auto midWord = fuzzyMatch("doc", "abcdocxyz.txt");
  ASSERT_TRUE(boundary.matched);
  ASSERT_TRUE(midWord.matched);
  EXPECT_GT(boundary.score, midWord.score);
}

TEST(FuzzyMatcher, PenalizesGapsBetweenMatchedCharacters) {
  const auto tight = fuzzyMatch("ab", "ab.txt");
  const auto wide = fuzzyMatch("ab", "a-long-gap-b.txt");
  ASSERT_TRUE(tight.matched);
  ASSERT_TRUE(wide.matched);
  EXPECT_GT(tight.score, wide.score);
}

TEST(FuzzyMatcher, ReturnsNoMatchForAnEmptyQueryOrCandidate) {
  EXPECT_FALSE(fuzzyMatch("", "example.txt").matched);
  EXPECT_FALSE(fuzzyMatch("ex", "").matched);
}

TEST(FuzzyMatcher, ReturnsNoMatchWhenQueryIsLongerThanCandidate) {
  EXPECT_FALSE(fuzzyMatch("toolongquery", "a.txt").matched);
}

TEST(FuzzyMatcher, PositionsAreAscendingAndWithinBounds) {
  const auto match = fuzzyMatch("elt", "example.txt");
  ASSERT_TRUE(match.matched);
  ASSERT_EQ(match.positions.size(), 3);
  for (int i = 1; i < match.positions.size(); ++i) {
    EXPECT_LT(match.positions[i - 1], match.positions[i]);
  }
  for (const int pos : match.positions) {
    EXPECT_GE(pos, 0);
    EXPECT_LT(pos, QStringLiteral("example.txt").size());
  }
}
