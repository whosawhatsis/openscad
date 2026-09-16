#pragma once

#include <QList>
#include <QString>

#include "gui/CompletionItem.h"

/**
 * @brief Where a candidate came from. Nearer sources are offered first.
 */
enum class CompletionSource {
  CurrentFile,
  Imported,
  Builtin,
};

struct CompletionCandidate {
  CompletionItem item;
  CompletionSource source = CompletionSource::Builtin;
  /// Named argument already given at this call site; still offered, but demoted.
  bool alreadySupplied = false;
};

/**
 * @brief How well a candidate answers a query. Higher is better; negative is no match.
 *
 * Case-insensitive throughout, with exact case preferred as a tie-break. An exact
 * name beats a prefix; anything that is not a prefix does not match at all. See
 * the TODO in the implementation for why subsequence matching is deferred.
 */
int completionMatchScore(const QString& candidate, const QString& query);

/**
 * @brief Drop non-matches and order what is left.
 *
 * Match quality first, then source, then whether a named argument has already been
 * given, and finally alphabetically so the order is total rather than arbitrary.
 */
QList<CompletionCandidate> rankCandidates(QList<CompletionCandidate> candidates, const QString& query);
