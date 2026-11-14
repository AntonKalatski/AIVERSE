// Map-level overrides of rules blocks (map "rules", docs/game-format.md § Per-map rules overrides).
#pragma once
#include "../../engine/data/Json.h"

namespace aiverse::game {

// Returns `rules` with the map's "rules" patch merged over it: objects merge key by key (recursively), any other
// value (numbers, strings, lists such as an xp table) replaces the rules value, `null` removes the key. Only the
// blocks / keys listed in mapRulesAllowed() may be patched; anything else throws a readable error
// ("map rules.types: ..."). A map without "rules" returns a null Json (the caller keeps its rules).
Json applyMapRules(const Json& rules, const Json& map);

// The patchable paths, for errors and docs: "block" (the whole block) or "block.key".
const char* mapRulesAllowed();

} // namespace aiverse::game
