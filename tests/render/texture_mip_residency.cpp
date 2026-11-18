#include "engine/gfx/TextureMipResidency.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool yes, const char* message) {
    if (!yes) throw std::runtime_error(message);
}

void cubemapCompleteness() {
    TextureMipResidency residency;
    TextureDesc d;
    d.width = d.height = 8;
    d.dimension = TextureDimension::Cube;
    d.arrayLayers = 6;
    d.mipLevels = 4;
    require(residency.initialize(d), "init cubemap residency");
    require(residency.mipLevels() == 4 && residency.layerCount() == 6,
            "face and mip geometry");
    require(!residency.sampleable() &&
            residency.finestResidentMip() == 4, "initial image cannot be sampled");

    std::vector<TextureMipRequest> coarse;
    for (int face = 0; face < 6; ++face) {
        const TextureMipRequest req = residency.claimNext();
        require(req.valid() && req.mip == 3 && req.layer == face,
                "all coarsest faces claimed before finer levels");
        coarse.push_back(req);
    }
    require(residency.pendingCount() == 6 &&
            residency.residentSliceCount() == 0, "coarse claims tracked");
    SamplerDesc base, sampled;
    require(!residency.residentSampler(base, sampled),
            "inflight faces cannot be sampled");
    for (int face = 0; face < 5; ++face)
        require(residency.complete(coarse[size_t(face)], true),
                "individual coarse face completed");
    require(!residency.sampleable(), "one missing face prevents sampling");
    require(residency.complete(coarse[5], true), "last coarse face");
    require(residency.sampleable() && residency.finestResidentMip() == 3,
            "coarsest level becomes sampleable");
    require(residency.residentSampler(base, sampled) &&
            sampled.minLod == 3 && sampled.maxLod == 3,
            "sampler restricted to fully resident mip");

    for (int face = 0; face < 6; ++face) {
        auto req = residency.claimNext();
        require(req.mip == 2 && req.layer == face, "request next finer mip");
        require(residency.complete(req, true), "finish mip2 face");
    }
    require(residency.finestResidentMip() == 2 &&
            residency.residentSampler(base, sampled) &&
            sampled.minLod == 2 && sampled.maxLod == 3,
            "sampler extends only into complete finer levels");
    auto failure = residency.claimNext();
    require(failure.mip == 1 && failure.layer == 0, "claim mip1 layer0");
    require(residency.complete(failure, false), "failed GPU write makes face retryable");
    auto retry = residency.claimNext();
    require(retry.mip == failure.mip && retry.layer == failure.layer &&
            retry.generation == failure.generation, "retry exact failed face");
    require(residency.complete(retry, true) &&
            !residency.complete(retry, true), "completion accepted only once");
    require(residency.finestResidentMip() == 2,
            "partially resident finer level does not widen clamp");
    require(residency.invalidate(3, 0), "coarse mip invalidate");
    require(!residency.sampleable(), "loss of coarse face makes image unsafe");
    require(!residency.residentSampler(base, sampled),
            "invalidated texture must not supply a sampler");
    require(!residency.invalidate(4, 0) && !residency.invalidate(0, 6),
            "invalid mip/layer bounds");
}

void volumeAndGeneration() {
    TextureMipResidency volume;
    TextureDesc d;
    d.width = d.height = 8;
    d.depth = 8;
    d.dimension = TextureDimension::Texture3D;
    d.mipLevels = 4;
    require(volume.initialize(d) && volume.layerCount() == 1,
            "3D volume uses one request per full mip volume");
    auto previous = volume.claimNext();
    require(previous.mip == 3 && previous.layer == 0 &&
            volume.complete(previous, true) && volume.sampleable(),
            "3D coarse volume upload completion");
    volume.reset();
    require(!volume.complete(previous, true),
            "delayed callback from old generation must be ignored");
    require(volume.initialize(d), "reinitialize new logical image");
    auto next = volume.claimNext();
    require(next.valid() && next.generation != previous.generation &&
            next.mip == 3 && next.layer == 0,
            "new resource tokens do not reuse generation");
    require(volume.complete(next, true), "new completion accepted");

    d = {};
    d.width = 8; d.height = 0;
    require(!volume.initialize(d), "invalid zero-height descriptor rejected");
    require(!volume.sampleable(), "invalid initialization clears former residency");
}

void emptyAndSamplerValidation() {
    TextureMipResidency planner;
    TextureDesc d;
    d.width = 16; d.height = 8;
    d.mipLevels = 1;
    require(planner.initialize(d), "one mip 2D initialized");
    auto req = planner.claimNext();
    require(req.mip == 0 && req.layer == 0, "single-mip claim");
    require(!planner.claimNext().valid(), "inflight claim not issued twice");
    SamplerDesc base, sampled;
    base.maxAnisotropy = 0;
    require(planner.complete(req, true) &&
            !planner.residentSampler(base, sampled),
            "invalid sampler description refused");
    base.maxAnisotropy = 1;
    require(planner.residentSampler(base, sampled) &&
            sampled.minLod == 0 && sampled.maxLod == 0 &&
            planner.fullyResident(), "complete 2D texture sampler");
}
}

int main() {
    try {
        cubemapCompleteness();
        volumeAndGeneration();
        emptyAndSamplerValidation();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Texture mip residency contract FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("Texture mip residency ordering, safety and sampler LOD: OK");
    return 0;
}
