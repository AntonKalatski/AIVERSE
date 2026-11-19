// VIVID real gfx::Device Metal RHI integration: not a fake device.
// Uses the engine's createMetalDevice / beginFrame / upload / compute /
// endFrame / diagnostic readBuffer path against the CPU CSR gather reference.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include "engine/character/vivid/GpuMorph.h"
#include "engine/gfx/Gfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

// MetalShaderAssets uses the regular platform content reader for optional
// precompiled archives. Standalone graphics tests have no content root.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    SparseMorphLibrary library;
    std::vector<MorphVertex> base;
    std::vector<float> weights;
};
Fixture makeFixture() {
    constexpr std::uint32_t vertexCount = 79;
    constexpr std::uint32_t shapeCount = 160;
    std::vector<MorphTargetInput> targets;
    targets.reserve(shapeCount);
    for (std::uint32_t shape = 0; shape < shapeCount; ++shape) {
        MorphTargetInput input;
        input.name = "rhi_morph_" + std::to_string(shape);
        for (std::uint32_t influence = 0; influence < 3; ++influence) {
            MorphDelta d{};
            d.vertex = (shape * 13u + influence * 29u) % vertexCount;
            d.position = {
                (float(int(shape % 11u) - 5) * 0.002f) * float(influence + 1),
                float(influence + 1) * 0.00075f,
                float(shape % 7u) * -0.0003f
            };
            d.normal = {0.002f * float(influence + 1), 0.0f, float(shape % 5u) * 0.0004f};
            d.tangent = {0.0f, float(shape % 3u) * 0.0005f, 0.0f};
            input.deltas.push_back(d);
        }
        targets.push_back(std::move(input));
    }
    Fixture f{SparseMorphLibrary::compile(vertexCount, std::move(targets)), {}, {}};
    f.base.resize(vertexCount);
    f.weights.resize(shapeCount);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        f.base[vertex].position = {float(vertex) * 0.012f, float(vertex % 9u) * -0.015f, float(vertex % 13u) * 0.021f};
        f.base[vertex].normal = {0, 1, 0};
        f.base[vertex].tangent = {1, 0, 0};
    }
    for (std::uint32_t shape = 0; shape < shapeCount; ++shape)
        f.weights[shape] = float((shape * 31u) % 101u) / 100.0f;
    return f;
}
float componentError(const MorphVec3& reference, const MorphVec3& actual) {
    float maximum = 0.0f;
    for (std::size_t c = 0; c < 3; ++c) {
        require(std::isfinite(actual[c]), "VIVID Metal RHI emitted nonfinite component");
        maximum = std::max(maximum, std::fabs(reference[c] - actual[c]));
    }
    return maximum;
}
void compare(const std::vector<MorphVertex>& expected, const std::vector<MorphVertex>& actual,
             std::uint32_t frame) {
    require(expected.size() == actual.size(), "VIVID Metal RHI vertex count mismatch");
    float maximum = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        maximum = std::max(maximum, componentError(expected[i].position, actual[i].position));
        maximum = std::max(maximum, componentError(expected[i].normal, actual[i].normal));
        maximum = std::max(maximum, componentError(expected[i].tangent, actual[i].tangent));
    }
    if (maximum > 0.0005f) {
        throw std::runtime_error("VIVID Metal RHI differs from CPU: frame=" +
            std::to_string(frame) + " max_abs_error=" + std::to_string(maximum));
    }
    std::cout << "VIVID Metal RHI frame=" << frame
              << " vertices=" << actual.size()
              << " max_abs_error=" << maximum << '\n';
}
void run() {
    @autoreleasepool {
        id<MTLDevice> metal = MTLCreateSystemDefaultDevice();
        require(metal != nil, "Metal device unexpectedly missing");
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = metal;
        layer.drawableSize = CGSizeMake(8, 8);
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        // The layer is intentionally headless: no render pass is needed by
        // the compute test, but gfx::Device owns and commits GPU frame queues.
        auto device = gfx::createMetalDevice((__bridge void*)layer);
        require(device != nullptr && device->supportsCompute(), "AIVERSE Metal RHI compute unavailable");

        const Fixture f = makeFixture();
        require(f.library.targets() == 160 && f.library.vertices() == 79, "fixture was truncated");
        GpuMorphDeformer deformer;
        require(deformer.initialize(*device, f.library, f.base),
                "VIVID RHI GPU initialize failed");
        require(deformer.ready() && deformer.outputBuffer().valid(),
                "VIVID RHI deformer output missing");

        std::vector<float> weights = f.weights;
        for (std::uint32_t frame = 0; frame < 4; ++frame) {
            if (frame == 1)
                std::fill(weights.begin(), weights.end(), 0.0f);
            if (frame == 2)
                for (std::size_t i = 0; i < weights.size(); ++i)
                    weights[i] = float(i % 3u) * 0.5f;
            const bool normalizeDirections = frame != 3;
            device->beginFrame(8, 8);
            const bool submitted = deformer.dispatch(weights, normalizeDirections);
            device->endFrame();
            if (!submitted)
                throw std::runtime_error("VIVID RHI dispatch failed on frame " +
                    std::to_string(frame) + ": " + deformer.lastError());
            require(device->gpuHealthy(), "AIVERSE Metal RHI reported GPU failure");
            std::vector<MorphVertex> output(f.base.size());
            require(deformer.download(output), "VIVID Metal RHI synchronous readback failed");
            std::vector<MorphVertex> expected(f.base.size());
            require(f.library.evaluateGather(f.base, weights, expected, normalizeDirections),
                    "VIVID RHI CPU reference failed");
            compare(expected, output, frame);
        }
        deformer.release(); // Must precede device destruction.
        require(!deformer.ready() && !deformer.outputBuffer().valid(),
                "VIVID RHI output handle leaked after release");
    }
}
}

int main() {
    @autoreleasepool {
        if (MTLCreateSystemDefaultDevice() == nil) {
            std::cout << "SKIP: no Metal-capable device\n";
            return 77;
        }
        try {
            run();
            std::cout << "VIVID real Metal gfx::Device parity PASS\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "VIVID real Metal gfx::Device parity FAIL: " << e.what() << '\n';
            return 1;
        }
    }
}
