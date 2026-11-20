// RtsView terrain entry points. The surface math lives in RtsTerrainSurface.h (shared with the
// editor core), the chunked mesh / materials / water in RtsTerrainRender.cpp.
#include "../RtsModule.h"
#include "RtsTerrainSurface.h"
#include "RtsView.h"
#include "engine/view/Surface.h"
#include "RtsRoadView.h"
#include "RtsShoreView.h"
#include "engine/render/OwnerPrepareCommit.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::game {

using namespace render;

// The single height query of the view: models, sprites, overlays, lights, editor gizmos. Matches the
// mesh: heightfield corners (two triangles per cell) when the map has them, otherwise levels + fine
// heights + ramps.
float RtsView::heightAt(const MapGrid& g,float x,float z) const {
    // No flat-map mesh has been published before the first build, and
    // planet/cube-sphere maps use their own surface policy. In both cases
    // retain the original Rules-derived visual height calculation.
    if(planets_.active() || !terrain_.built())
        return terrain::heightAt(
            g,rules_?float(rules_->levelHeight.toDouble()):1.5f,x,z);
    // The editor can change MapGrid corner/level data IN PLACE long before
    // the replacement GPU owner cohort publishes. Only the snapshot of the
    // actually published Terrain is visible to scene clients and cameras.
    return terrain_.surfaceAt(g,x,z);
}

float RtsView::groundAt(const MapGrid& g, float x, float z) const {
    // Genre-owned height policies projected into one generic,
    // allocation-free surface composition helper. All GPU and sim
    // entities still use the same ground placement as before.
    return ::aiverse::view::composeSurfaceHeight(x,z,
        [this,&g](float px,float pz){return heightAt(g,px,pz);},
        [this](float px,float pz,float terrain) {
            return roads_ && roads_->hasSurface()
                ? roads_->surfaceAt(px,pz,terrain):terrain;
        });
}

void RtsView::setTerrainDetail(const Json& map) {
    discardPendingSurfaceLease();
    ++terrainGeneration_; // includes surface policy/texture changes before rebuilding geometry
    setRoadMap(map);
    terrain::DetailSurface d;
    std::vector<uint8_t> splat;
    std::string err;
    if (terrain::loadMapDetail(map, read_, d, splat, &err)) terrain_.setDetail(&d, &splat);
    else {
        if (!err.empty()) std::fprintf(stderr, "[terrain] %s\n", err.c_str());
        terrain_.setDetail(nullptr);
    }
}

void RtsView::buildTerrain(gfx::Device& dev,const MapGrid& g) {
    buildTerrainImpl(dev,g,false);
}
void RtsView::buildTerrainAtomic(gfx::Device& dev,const MapGrid& g) {
    buildTerrainImpl(dev,g,true);
}
void RtsView::buildTerrainImpl(gfx::Device& dev,const MapGrid& g,
                               bool forceAtomic) {
    discardPendingSurfaceLease();
    // Any map rebuild invalidates the previous primary-frame publication.
    framePublication_.invalidate();
    ++terrainGeneration_; // conservative invalidation: cell edits can retain the MapGrid address
    if (planets_.active()) {
        paintLayers_.shutdown(); // planet maps: the grid holds the stacked faces of every body (RtsSurface.h)
        const PlanetWorld& W = planets_.world();
        if (W.sim && g.surf == W.sim) // cell flags changed (editor play / sim): recolour those bodies
            for (size_t i = 0; i < W.bodies.size(); ++i) {
                if (!W.bodies[i].surface()) continue;
                const int N = W.bodies[i].faceSize;
                std::vector<uint8_t> f(size_t(6 * N * N));
                for (int fc = 0; fc < 6; ++fc)
                    for (int y = 0; y < N; ++y)
                        for (int x = 0; x < N; ++x) f[(size_t(fc) * size_t(N) + size_t(y)) * size_t(N) + size_t(x)] = g.flags[size_t(W.sim->gridCell(int(i), fc, x, y))];
                planets_.updateFlags(int(i), f);
            }
        fog_.build(dev, g.w, g.h, [](float, float) { return 0.0f; });   // field only (the sheet is not drawn)
        hasTerrain_ = true;
        return;
    }
    const bool flatDig = groundFx_.drawsDig() && g.dig.size() == g.flags.size(); // RtsGroundFx draws the rock blocks
    // The initial build and same-map brush edits keep their existing
    // synchronous path. A topology change stages the NEW Terrain surface
    // first, then uses that exact unpublished height for both road wheel
    // samples and shore geometry before any owner commits.
    const bool sameDimensions=terrain_.mapMatches(g.w,g.h);
    const bool sculptTransaction=forceAtomic && sameDimensions;
    // Same-sized terrain might not have a Fog sheet yet (e.g. the first
    // editor build before any primary draw). Sheet-only publication is
    // legal only when there is already a matching R8 texture and mesh.
    const bool preserveFog=sculptTransaction &&
        fog_.sheetMatches(g.w,g.h);
    const bool topologyChange=terrain_.canPrepareMap() &&
        (!sameDimensions || forceAtomic);
    bool changed=false;
    if(topologyChange) {
        std::shared_ptr<TerrainRenderer::PreparedMap> stagedTerrain;
        std::shared_ptr<RtsRoadView::PreparedRebuild> stagedRoad;
        std::shared_ptr<RtsShoreView::PreparedRebuild> stagedShore;
        std::shared_ptr<RtsGroundFx::PreparedFrame> stagedGroundFx;
        std::unique_ptr<FogOfWar::PreparedSheet> stagedFogSheet;
        std::unique_ptr<FogOfWar::PreparedUpdate> stagedFogField;
        // No scene mask is available at this synchronous editor/client map
        // boundary. Initialize a private all-unexplored field (never reveal
        // unknown map cells); the next primary draw stages actual viewer Fog.
        const std::vector<uint8_t> unexplored(
            size_t(g.w)*size_t(g.h),uint8_t(0));
        const std::function<float(float,float)> newHeight=
            [&](float x,float z) {
                return terrain_.preparedSurfaceAt(stagedTerrain,g,x,z);
            };
        // Generic coordinator remains independent of RTS and map policies.
        // Every participant stores only a candidate until publish().
        struct Part final:render::OwnerPrepareCommit::Participant {
            std::function<void()> prepareFn,commitFn,rollbackFn;
            Part(std::function<void()> p,std::function<void()> c,
                 std::function<void()> r)
                :prepareFn(std::move(p)),commitFn(std::move(c)),
                 rollbackFn(std::move(r)){}
            void prepare() override {prepareFn();}
            void commit() noexcept override {commitFn();}
            void rollback() noexcept override {rollbackFn();}
        };
        Part terrainPart(
            [&] {
                stagedTerrain=terrain_.prepareMap(dev,g,flatDig);
                if(!stagedTerrain)
                    throw std::runtime_error("RtsView: empty topology candidate");
            },
            [&] {(void)terrain_.commitMap(std::move(stagedTerrain));},
            [&] {stagedTerrain.reset();});
        Part roadPart(
            [&] {stagedRoad=roads_->prepareRebuild(dev,newHeight,true);},
            [&] {roads_->commitRebuild(std::move(stagedRoad));},
            [&] {stagedRoad.reset();});
        Part shorePart(
            [&] {
                stagedShore=shores_->prepareRebuild(
                    dev,newHeight,terrain_.look().waterOffset,g.w,g.h,true);
            },
            [&] {shores_->commitRebuild(std::move(stagedShore));},
            [&] {stagedShore.reset();});
        Part groundFxPart(
            [&] {
                // No camera is needed to atomically replace the cell grid.
                // Meshes will be built by the first scene-aware primary frame.
                stagedGroundFx=groundFx_.prepareMapReset(
                    g,sculptTransaction);
            },
            [&] {groundFx_.commitFrame(std::move(stagedGroundFx));},
            [&] {stagedGroundFx.reset();});
        Part fogPart(
            [&] {
                if(!fog_.ready())
                    throw std::runtime_error("RtsView: fog pipeline unavailable for map change");
                stagedFogSheet=fog_.prepareSheet(dev,g.w,g.h,newHeight);
                if(!stagedFogSheet)
                    throw std::runtime_error("RtsView: fog sheet map staging failed");
                if(!preserveFog) {
                    // A map resize has no viewer mask at this synchronous
                    // boundary, so publish an unexplored baseline. Same
                    // dimensions MUST preserve all learned Fog visibility.
                    stagedFogField=fog_.prepareUpdate(
                        unexplored,g.w,g.h,0.0,stagedFogSheet.get());
                    if(!stagedFogField)
                        throw std::runtime_error("RtsView: fog field map staging failed");
                }
            },
            [&] {
                if(preserveFog) {
                    (void)fog_.commitPreparedSheet(
                        std::move(stagedFogSheet));
                } else {
                    (void)fog_.commitPreparedFrame(
                        std::move(stagedFogSheet),std::move(stagedFogField));
                }
            },
            [&] {
                stagedFogField.reset();
                stagedFogSheet.reset();
            });
        render::OwnerPrepareCommit tx;
        tx.add(terrainPart);
        tx.add(roadPart);
        tx.add(shorePart);
        tx.add(groundFxPart);
        tx.add(fogPart); // a late R8 upload failure rolls back all owners
        // An editor or GPU callback can update MapGrid or mutate an owner
        // while the private GPU builders are running. Never commit a
        // mixture of topology generations from different input revisions.
        // The fingerprint also covers same-address, same-size brush edits.
        const uint64_t inputFingerprint=mapSourceFingerprint(g);
        const uint64_t originalGeneration=terrainGeneration_;
        const std::array<uint64_t,5> ownerRevisions={
            terrain_.stateRevision(),roads_->stateRevision(),
            shores_->stateRevision(),groundFx_.stateRevision(),
            fog_.stateRevision()};
        tx.prepareAll();
        const std::array<uint64_t,5> finalRevisions={
            terrain_.stateRevision(),roads_->stateRevision(),
            shores_->stateRevision(),groundFx_.stateRevision(),
            fog_.stateRevision()};
        if(inputFingerprint!=mapSourceFingerprint(g) ||
           originalGeneration!=terrainGeneration_ ||
           ownerRevisions!=finalRevisions || dev_!=&dev ||
           planets_.active()) {
            // OwnerPrepareCommit rolls back in reverse order; not one live
            // owner or retired GPU queue is published by this attempt.
            tx.rollback();
            throw std::runtime_error(
                "RtsView: map source changed during topology preparation");
        }
        if(!tx.publish())
            throw std::logic_error("RtsView: topology cohort not published");
        changed=true;
    }else {
        changed=terrain_.build(dev,g,flatDig);
        // Same-size sculpting changes road heights even when no road-network
        // JSON changed. Trigger the next primary-frame owner cohort to
        // resample the geometry using the now-published terrain.
        if(changed) {
            roads_->invalidateGround();
            shores_->invalidateGround();
        }
    }
    // Paint, GroundFX, field overlays and Fog will rebuild only after the
    // terrain/road/shore topology has been successfully published.
    paintLayers_.shutdown();
    if(changed || !hasTerrain_)groundFx_.invalidate();
    hasTerrain_ = true;
    // Both full-size map transitions and explicit same-size sculpt cohorts
    // already published the correctly draped Fog Sheet. Only the sparse
    // legacy same-size path needs a Fog Sheet rebuild on next primary draw.
    fogMeshDirty_ = !topologyChange;
    fieldView_.invalidate();  // so do grid field overlays
    if (twoD_) twoD_->buildTiles(dev, g, rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f);
}

} // namespace aiverse::game
