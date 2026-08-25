#include "gui/CompletionRanking.h"

#include <algorithm>

int completionMatchScore(const QString& candidate, const QString& query)
{
  if (query.isEmpty()) return 0;

  if (candidate == query) return 100;
  if (candidate.compare(query, Qt::CaseInsensitive) == 0) return 90;
  if (candidate.startsWith(query, Qt::CaseSensitive)) return 80;
  if (candidate.startsWith(query, Qt::CaseInsensitive)) return 70;

  // TODO: non-contiguous subsequence matching ("lex" -> linear_extrude) is
  // deliberately not implemented. It cannot be delivered through QScintilla's
  // AcsAPIs path: Scintilla dismisses the popup outright when nothing in the list
  // starts with the typed word, so a subsequence candidate removes the popup
  // rather than adding to it. Reaching it needs the list driven by
  // QsciScintilla::showUserList(), which does not sort and does not prefix-match -
  // a rework of triggering, prefix replacement and call tips. Scoring it here in
  // the meantime would only produce candidates the caller has to throw away.
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

                     // Shapes sit with the call they belong to, never above it: the
                     // bare structure stays the default and the seeded variants
                     // follow it.
                     const bool shapeA = a.item.kind() == CompletionItem::Kind::ArgumentShape;
                     const bool shapeB = b.item.kind() == CompletionItem::Kind::ArgumentShape;
                     if (shapeA != shapeB) return shapeB;
                     if (shapeA && a.item.shape() != b.item.shape()) {
                       return a.item.shape() < b.item.shape();
                     }

                     // A total order: case-insensitive first so "Size" and "size" sit
                     // together, then case-sensitive so the result never depends on
                     // the order they happened to arrive in.
                     const int byName = a.item.label().compare(b.item.label(), Qt::CaseInsensitive);
                     if (byName != 0) return byName < 0;
                     return a.item.label() < b.item.label();
                   });

  return matches;
}
