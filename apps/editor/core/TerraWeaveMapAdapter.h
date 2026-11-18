// Optional editor adapter: bake TerraWeave into the EXISTING MapDoc heightfield.
// The renderer and navmesh continue using their usual dirty-revision paths.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/world/TerraWeaveHeightmap.h"
#include <stdexcept>
#include <utility>

namespace aiverse::editor {

enum class TerraWeaveExistingTerrain : uint8_t { RefuseOverwrite, ReplaceExplicitly };

inline void bakeTerraWeaveTerrain(MapDoc& doc,
        const terraweave::TerrainSettings& terrain,
        terraweave::LegacyHeightfieldOptions origin = {},
        TerraWeaveExistingTerrain overwrite =
            TerraWeaveExistingTerrain::RefuseOverwrite) {
    if (doc.data.planet())
        throw std::invalid_argument("TerraWeave flat MapDoc adapter does not support planets");
    // The existing detail surface wins over corners in the renderer.
    // Refuse hidden regeneration unless the caller explicitly accepts clearing it.
    const bool authoredSurface = !doc.data.corners.empty() ||
        (doc.data.detailScale > 0 && !doc.data.detail.empty());
    if (authoredSurface && overwrite == TerraWeaveExistingTerrain::RefuseOverwrite)
        throw std::invalid_argument("TerraWeave refuses to overwrite authored terrain/detail");
    // Build temporary buffer first; exceptions must never touch the live document.
    std::vector<int16_t> generated = terraweave::generateLegacyCorners(
        terrain, doc.data.w, doc.data.h, origin);
    doc.beginEdit();                  // undo snapshot
    doc.data.corners = std::move(generated);
    // Existing detail samples have higher render priority than corners.
    // Clearing them is necessary for the generated terrain to become visible.
    // MapDoc::beginEdit() captured the original detail/splat for Undo.
    doc.data.detailScale = 0;
    doc.data.detail.clear();
    doc.data.detailSplat.clear();
    doc.data.detailFile.clear();
    doc.touched();                    // existing view rebuild policy
    // Does not change rules.terrain.model or terrain.detail: the caller
    // explicitly chooses whether corners are gameplay or visual only.
}

} // namespace aiverse::editor
