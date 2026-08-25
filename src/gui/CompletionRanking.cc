#include "gui/CompletionRanking.h"

#include <algorithm>

namespace {

// Every character of the query appears in the candidate, in order.
bool isSubsequence(const QString& candidate, const QString& query)
{
  int c = 0;
  for (const QChar q : query) {
    while (c < candidate.size() && candidate.at(c).toLower() != q.toLower()) ++c;
    if (c == candidate.size()) return false;
    ++c;
  }
  return true;
}

}  // namespace

int completionMatchScore(const QString& candidate, const QString& query)
{
  if (query.isEmpty()) return 0;

  if (candidate == query) return 100;
  if (candidate.compare(query, Qt::CaseInsensitive) == 0) return 90;
  if (candidate.startsWith(query, Qt::CaseSensitive)) return 80;
  if (candidate.startsWith(query, Qt::CaseInsensitive)) return 70;
  if (isSubsequence(candidate, query)) return 40;
  return -1;
}

QList<CompletionCandidate> rankCandidates(QList<CompletionCandidate> candidates, const QString& query)
{
  QList<CompletionCandidate> matches;
  for (auto& candidate : candidates) {
    if (completionMatchScore(candidate.item.label(), query) >= 0) matches.append(candidate);
  }

  std::stable_sort(matches.begin(), matches.end(),
                   [&query](const CompletionCandidate& a, const CompletionCandidate& b) {
                     const int scoreA = completionMatchScore(a.item.label(), query);
                     const int scoreB = completionMatchScore(b.item.label(), query);
                     if (scoreA != scoreB) return scoreA > scoreB;

                     if (a.source != b.source) return a.source < b.source;

                     if (a.alreadySupplied != b.alreadySupplied) return b.alreadySupplied;

                     // A total order: case-insensitive first so "Size" and "size" sit
                     // together, then case-sensitive so the result never depends on
                     // the order they happened to arrive in.
                     const int byName = a.item.label().compare(b.item.label(), Qt::CaseInsensitive);
                     if (byName != 0) return byName < 0;
                     return a.item.label() < b.item.label();
                   });

  return matches;
}
