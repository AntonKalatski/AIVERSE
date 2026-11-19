// VIVID scalable groom cards, deterministic guide topology and local
// presentation-only secondary motion. Supports mobile card LOD fallback.
#pragma once
#include "HumanDNA.h"
#include "engine/render/Model.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aiverse::vivid {
struct GroomGuide {
    render::float3 root{}, normal{};
    std::vector<render::float3> rest, position, previous;
};
enum class GroomQuality : std::uint8_t { Low, Medium, High };
struct GroomStats {
    std::size_t guides=0,segments=0,triangles=0;
    float updatedMs=0.0f;
};
class HumanGroomRuntime {
public:
    // Distributes root guides across an ellipsoidal scalp cap using a
    // stable low-discrepancy pattern. Does NOT grow scalp geometry into eyes.
    void generate(const HumanDNA& dna, GroomQuality quality=GroomQuality::High);
    // Fixed-bounded verlet + constraint projection and coarse scalp
    // collision. Steps at most 1/20 second and never mutates simulation ECS.
    bool step(float dt,render::float3 wind={});
    std::unique_ptr<render::Model> makeCardModel(const HumanDNA& dna) const;
    bool updateCardVertices(std::span<render::ModelVertex> output) const;
    std::size_t vertexCount() const noexcept {return guides_.size()*(segments_+1)*2;}
    std::size_t triangleCount() const noexcept {return guides_.size()*segments_*2;}
    const std::vector<GroomGuide>& guides() const noexcept {return guides_;}
    GroomStats stats() const noexcept {return {guides_.size(),segments_,triangleCount(),lastStepMs_};}
private:
    std::vector<GroomGuide> guides_;
    std::size_t segments_=0;
    float stiffness_=0.55f,damping_=0.25f,gravity_=0.65f,windStrength_=0.15f;
    // Scalp ellipsoid matches the DNA-generated guide root geometry.
    // Stored per instance, not inferred from a fixed human head preset.
    render::float3 scalpRadii_{0.72f,0.90f,0.75f};
    float lastStepMs_=0;
};
} // namespace aiverse::vivid
