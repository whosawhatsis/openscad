#pragma once

#include <string>
#include <vector>

/**
 * @brief Curated call shapes offered alongside a callable's bare structure.
 *
 * Each entry is a complete, valid call whose arguments are seeded with the
 * operation's identity or unit value - translate with a zero vector, scale with
 * ones - so accepting one changes nothing until a field is edited, and any field
 * left alone is still correct.
 *
 * This lives in core rather than in the editor because the values are properties
 * of the operation, not of the editor: cylinder's practical starting point is not
 * a fact about completion. It is a hand-written table today; the intent recorded
 * with the specification is that it eventually be generated alongside the builtin
 * registrations and exportable to other editors, which is why callers should treat
 * it as canonical metadata rather than a completion heuristic.
 *
 * @return Shapes for the callable, or an empty vector when none is curated.
 *         Absence is normal and means "offer only the bare structure".
 */
const std::vector<std::string>& argumentShapesFor(const std::string& name);
