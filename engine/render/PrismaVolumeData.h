#pragma once
// GPU-free PRISMA volume-profile parsing. Per-map overrides are resolved by stable id.
// External serialized key: "prismaVolumes"; existing "postfx" format is not changed.
#include "PrismaVolumes.h"

namespace aiverse::render {

// game and map are each the corresponding "prismaVolumes" block, not the whole JSON document.
// Shape: global | sphere | box; each volume holds a "profile" with the existing postfx schema.
// Map volumes with a matching id replace game volumes; map {"replace":true} discards all game volumes.
// Invalid entries are skipped and reported in issues. This must never mutate authored JSON.
PrismaVolumeStack parsePrismaVolumes(const Json& game, const Json& map,
                                    std::vector<std::string>* issues = nullptr);

} // namespace aiverse::render
