#include <catch2/catch_test_macros.hpp>

#include <QStringList>

#include "gui/CompletionRanking.h"

namespace {

CompletionCandidate cand(const char *name, CompletionItem::Kind kind = CompletionItem::Kind::Function,
                         CompletionSource source = CompletionSource::Builtin, bool supplied = false)
{
  CompletionCandidate c{CompletionItem(name, kind), source, supplied};
  return c;
}

QStringList order(QList<CompletionCandidate> candidates, const QString& query)
{
  QStringList names;
  for (const auto& c : rankCandidates(std::move(candidates), query)) names << c.item.label();
  return names;
}

}  // namespace

TEST_CASE("match quality orders candidates before anything else", "[completion]")
{
  // An exact match beats a prefix match.
  CHECK(order({cand("sin"), cand("si")}, "si") == QStringList{"si", "sin"});
  CHECK(order({cand("sphere")}, "si").isEmpty());

  // Case matters only as a tie-break: both match, the exact case first.
  CHECK(order({cand("Size"), cand("size")}, "size") == QStringList{"size", "Size"});

  // A prefix outranks a subsequence, which is still offered below it.
  CHECK(order({cand("linear_extrude"), cand("len")}, "le") == QStringList{"len", "linear_extrude"});

  // Non-matches are dropped entirely.
  CHECK(order({cand("cube"), cand("sphere")}, "zz").isEmpty());

  // Prefix matching is case-insensitive.
  CHECK(order({cand("linear_extrude")}, "LIN") == QStringList{"linear_extrude"});

  // Non-contiguous subsequences match: the popup is a user list, so we choose the
  // entries and Scintilla never has to match the typed word against them.
  CHECK(order({cand("linear_extrude")}, "lex") == QStringList{"linear_extrude"});
  CHECK(order({cand("linear_extrude")}, "LEX") == QStringList{"linear_extrude"});
  CHECK(order({cand("linear_extrude")}, "xel").isEmpty());  // order matters
}

TEST_CASE("equal matches fall back to source, then supplied, then alphabet", "[completion]")
{
  // Same match quality: what the current file declares comes before a builtin.
  CHECK(order({cand("cube", CompletionItem::Kind::LeafModule, CompletionSource::Builtin),
               cand("cubz", CompletionItem::Kind::LeafModule, CompletionSource::CurrentFile)},
              "cub") == QStringList{"cubz", "cube"});

  // Imported sits between the two.
  CHECK(order({cand("cub_b", CompletionItem::Kind::LeafModule, CompletionSource::Builtin),
               cand("cub_i", CompletionItem::Kind::LeafModule, CompletionSource::Imported),
               cand("cub_c", CompletionItem::Kind::LeafModule, CompletionSource::CurrentFile)},
              "cub") == QStringList{"cub_c", "cub_i", "cub_b"});

  // A named argument already given at this call site sinks below one still missing.
  CHECK(order({cand("center", CompletionItem::Kind::NamedParameter, CompletionSource::Builtin, true),
               cand("centre", CompletionItem::Kind::NamedParameter, CompletionSource::Builtin, false)},
              "cent") == QStringList{"centre", "center"});

  // Everything else equal, alphabetical - and it is a total order, never arbitrary.
  CHECK(order({cand("cubc"), cand("cuba"), cand("cubb")}, "cub") == QStringList{"cuba", "cubb", "cubc"});
}

TEST_CASE("an empty query keeps every candidate in a stable order", "[completion]")
{
  CHECK(order({cand("beta"), cand("alpha")}, "") == QStringList{"alpha", "beta"});
}
