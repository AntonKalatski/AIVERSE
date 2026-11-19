// SpectraFX lifecycle: authored emitter stages and CPU runtime adapter.
// The existing ParticleSystem retains ownership of particle simulation,
// collision, subemitters, sorting, budgets and RHI drawing. This layer controls
// instance start/stop/move and stage configuration, not a second particle solver.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxGraphRegistry.h"

namespace aiverse::fx {

enum class SpectraFxStage : uint8_t { Spawn, Initialize, Update, Render };

struct SpectraFxStageDef {
    SpectraFxStage stage = SpectraFxStage::Spawn;
    bool enabled = true;
    // Used by Spawn: an existing ParticleSystem effect name.
    std::string effect;
    // Initialize: initial instance scaling and tint.
    float scale = 1;
    float4 tint{1,1,1,1};
};

struct SpectraFxEmitterProgram {
    std::vector<SpectraFxStageDef> stages;

    // Each stage may appear at most once, ordered by lifecycle.
    // Spawn must supply an effect. Update/Render are contracts delegated
    // to ParticleSystem; disabling either does not suspend its internals.
    bool validate(std::string* error = nullptr) const {
        auto fail = [&](const char* reason) {
            if (error) *error = reason;
            return false;
        };
        if (stages.empty() || stages.size() > 4) return fail("invalid SpectraFX stage count");
        int last = -1;
        bool spawn = false;
        for (const auto& def : stages) {
            const int id = static_cast<int>(def.stage);
            if (id < 0 || id > 3 || id <= last) return fail("SpectraFX stages must be ordered and unique");
            last = id;
            if (def.stage == SpectraFxStage::Spawn) {
                if (!def.enabled || def.effect.empty() || def.effect.size() > 256)
                    return fail("SpectraFX Spawn requires a valid effect");
                spawn = true;
            }
            if (!std::isfinite(def.scale) || def.scale < 0 || def.scale > 1000)
                return fail("SpectraFX invalid initialize scale");
            const auto c = def.tint;
            if (!std::isfinite(c.x) || !std::isfinite(c.y) ||
                !std::isfinite(c.z) || !std::isfinite(c.w) ||
                c.x < 0 || c.x > 8 || c.y < 0 || c.y > 8 ||
                c.z < 0 || c.z > 8 || c.w < 0 || c.w > 8)
                return fail("SpectraFX invalid initialize tint");
        }
        if (!spawn) return fail("SpectraFX program needs Spawn stage");
        if (error) error->clear();
        return true;
    }

    SpectraFxStageDef* find(SpectraFxStage stage) {
        for (auto& d : stages) if (d.stage == stage) return &d;
        return nullptr;
    }
    const SpectraFxStageDef* find(SpectraFxStage stage) const {
        for (const auto& d : stages) if (d.stage == stage) return &d;
        return nullptr;
    }
};

// Session-scoped stable handles are supplied by callers; never use a bare
// recyclable ECS index. This adapter calls the existing ParticleSystem CPU
// backend directly and does not create/own a second particle buffer.
class SpectraFxCpuEmitterRuntime {
public:
    struct Stats {
        uint64_t starts = 0, failed = 0, replaced = 0;
        uint64_t moves = 0, stops = 0, kills = 0;
    };

    bool start(uint64_t instance, const SpectraFxEmitterProgram& program,
               ParticleSystem& particles, float3 pos,
               float3 dir = {0,1,0}, float scale = 1,
               float4 tint = {1,1,1,1}) {
        if (!instance || !program.validate()) { ++stats_.failed; return false; }
        const auto* spawn = program.find(SpectraFxStage::Spawn);
        const auto* init = program.find(SpectraFxStage::Initialize);
        if (!spawn) { ++stats_.failed; return false; }
        if (init && init->enabled) {
            scale *= init->scale;
            tint = {tint.x*init->tint.x,tint.y*init->tint.y,
                    tint.z*init->tint.z,tint.w*init->tint.w};
        }
        // Preserve a working instance if replacement fails to load/spawn.
        const FxHandle next = particles.play(spawn->effect,pos,dir,scale,tint);
        if (!next.valid()) { ++stats_.failed; return false; }
        auto it = active_.find(instance);
        if (it != active_.end()) {
            particles.kill(it->second);
            it->second = next;
            ++stats_.replaced;
        } else active_[instance] = next;
        ++stats_.starts;
        return true;
    }

    bool move(uint64_t instance,ParticleSystem& particles,
              float3 pos,float3 dir = {0,1,0}) {
        const auto it = active_.find(instance);
        if (it == active_.end()) return false;
        particles.move(it->second,pos,dir);
        ++stats_.moves; return true;
    }
    bool stop(uint64_t instance,ParticleSystem& particles,bool kill = false) {
        auto it = active_.find(instance);
        if (it == active_.end()) return false;
        if (kill) { particles.kill(it->second); ++stats_.kills; }
        else { particles.stop(it->second); ++stats_.stops; }
        active_.erase(it);
        return true;
    }
    // Explicit discontinuity barrier (rollback / scene unload / new match).
    // All owned instances are killed; other ParticleSystem effects untouched.
    void reset(ParticleSystem& particles) {
        for (const auto& pair : active_) particles.kill(pair.second);
        active_.clear();
    }

    bool owns(uint64_t instance) const { return active_.find(instance) != active_.end(); }
    size_t activeCount() const { return active_.size(); }
    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    std::unordered_map<uint64_t,FxHandle> active_;
    Stats stats_{};
};

} // namespace aiverse::fx
