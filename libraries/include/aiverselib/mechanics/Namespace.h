#pragma once
// Mechanics packages live in namespace aiverse::lib::mechanics (folder aiverselib/mechanics).
// `aiverse::lib::genre` is the legacy name of the same namespace, kept as an alias so existing code
// (`lib::genre::X`, `namespace lg = aiverse::lib::genre`) keeps compiling. New code uses `mechanics`.
// Mechanics are genre-independent: see AGENTS.md "NO GENRE SPLIT" and docs/no-genre-split-audit.md (G7).
namespace aiverse::lib::mechanics {}
namespace aiverse::lib { namespace genre = mechanics; }
