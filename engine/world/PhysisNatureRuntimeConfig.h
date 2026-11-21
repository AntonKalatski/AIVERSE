// PHYSIS nature runtime — data block and pure conversions (2026-11-22).
//
// A project that has a map-level "physis" block gets the nature presentation
// automatically: SpectraFX fire/smoke, fire/wind audio, TerraWeave vegetation
// wind, NEREID visual wind and PRISMA physical-sky weather. The optional
// "physis.presentation" object only tunes or switches parts off:
//
//   "physis": { ..., "presentation": {
//       "fx": true, "audio": true, "vegetationWind": true, "waterWind": true,
//       "sky": true, "radiusMeters": 160, "maxRegionCells": 96,
//       "fxWindScale": 0.35, "windFieldNodes": 16 } }
//
// "presentation": false switches the whole presentation off while the
// simulation still runs. Nothing here depends on a game genre, a renderer or a
// GPU; the functions only read a committed NaturePresentationFrame and return
// presentation values. They never write PHYSIS / TerraWeave / NEREID state.
#pragma once
#include "engine/data/Json.h"
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/world/TerraWeave.h"
#include "engine/world/TerraWeaveWindField.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace aiverse::world {

struct NatureRuntimeConfig {
    bool fx = true;              // SpectraFX flames / smoke / fire light
    bool audio = true;           // looping fire clusters + wind bed
    bool vegetationWind = true;  // TerraWeave Flora/Horizon wind (WorldScene)
    bool waterWind = true;       // NEREID visual wind (WorldScene)
    bool sky = true;             // PRISMA physical sky coverage / cloud drift
    float radiusMeters = 160;    // presentation region around the camera focus
    int maxRegionCells = 96;     // hard cap of PHYSIS cells per frame axis
    float fxWindScale = 0.35f;   // particle drift per m/s of PHYSIS wind
    int windFieldNodes = 16;     // spatial vegetation wind lattice; 0 = uniform
    bool any() const { return fx || audio || vegetationWind || waterWind || sky; }
};

// nullopt: no "physis" block, "physis.enabled": false or "presentation": false.
// Throws std::invalid_argument on malformed values (wrong type / out of range).
inline std::optional<NatureRuntimeConfig> natureRuntimeConfigFromMap(const Json& map) {
    const Json& physis = map["physis"];
    if (physis.isNull() || !physis.isObject()) return std::nullopt;
    if (!physis["enabled"].isNull() && physis["enabled"].type() != Json::Type::Bool)
        throw std::invalid_argument("PHYSIS enabled must be boolean");
    if (!physis.getBool("enabled", true)) return std::nullopt;
    NatureRuntimeConfig c;
    const Json& p = physis["presentation"];
    if (p.isNull()) return c;
    if (p.type() == Json::Type::Bool) {
        if (!p.boolean()) return std::nullopt;
        return c;
    }
    if (!p.isObject()) throw std::invalid_argument("PHYSIS presentation must be an object or boolean");
    const auto flag = [&](const char* key, bool& out) {
        const Json& v = p[key];
        if (v.isNull()) return;
        if (v.type() != Json::Type::Bool)
            throw std::invalid_argument(std::string("PHYSIS presentation flag must be bool: ") + key);
        out = v.boolean();
    };
    const auto number = [&](const char* key, double lo, double hi, double& out) {
        const Json& v = p[key];
        if (v.isNull()) return;
        if (!v.isNumber())
            throw std::invalid_argument(std::string("PHYSIS presentation number required: ") + key);
        const double d = v.fx().toDouble();
        if (!(d >= lo && d <= hi))
            throw std::out_of_range(std::string("PHYSIS presentation bounds: ") + key);
        out = d;
    };
    flag("fx", c.fx);
    flag("audio", c.audio);
    flag("vegetationWind", c.vegetationWind);
    flag("waterWind", c.waterWind);
    flag("sky", c.sky);
    double radius = c.radiusMeters, cells = c.maxRegionCells, scale = c.fxWindScale,
           nodes = c.windFieldNodes;
    number("radiusMeters", 10, 4000, radius);
    number("maxRegionCells", 2, 512, cells);
    number("fxWindScale", 0, 4, scale);
    number("windFieldNodes", 0, 64, nodes);
    if (nodes != 0 && nodes < 2)
        throw std::out_of_range("PHYSIS presentation windFieldNodes: 0 or 2..64");
    c.radiusMeters = float(radius);
    c.maxRegionCells = int(cells);
    c.fxWindScale = float(scale);
    c.windFieldNodes = int(nodes);
    return c;
}

namespace nature_runtime_detail {
inline std::int32_t cmPerSec(float metresPerSecond) {
    if (!std::isfinite(metresPerSecond)) return 0;
    return std::int32_t(std::lround(std::clamp(metresPerSecond * 100.f, -5000.f, 5000.f)));
}
inline std::uint32_t gustPermille(float speed) {
    if (!std::isfinite(speed)) return 350;
    return std::uint32_t(std::lround(std::clamp(250.f + speed * 25.f, 250.f, 900.f)));
}
} // namespace nature_runtime_detail

// Region-mean PHYSIS wind as a TerraWeave sway state (cycle / flex / lean of
// `base` are kept, only velocity and gust follow the weather).
inline terraweave::ForestWindState forestWindFromNature(
        const NaturePresentationFrame& frame, terraweave::ForestWindState base = {}) {
    if (!frame.valid) return base;
    using namespace nature_runtime_detail;
    base.xCmPerSec = cmPerSec(frame.meanWindX);
    base.zCmPerSec = cmPerSec(frame.meanWindZ);
    base.gustPermille = gustPermille(std::hypot(frame.meanWindX, frame.meanWindZ));
    terraweave::validateForestWind(base);
    return base;
}

// Spatial lattice over the frame region (nodes x nodes, 2..64), sampled from
// the bilinear PHYSIS wind incl. WindZone/WindShelter/storm contributions.
// Null when the frame is invalid, `nodes` is 0 or the region is degenerate.
inline std::shared_ptr<const terraweave::ForestWindField> forestWindFieldFromNature(
        const NaturePresentationFrame& frame, int nodes) {
    if (!frame.valid || nodes < 2 || frame.width <= 0 || frame.height <= 0) return nullptr;
    using namespace nature_runtime_detail;
    const int n = std::clamp(nodes, 2, 64);
    const double cell = double(frame.cellSize);
    const double minX = double(frame.originX) * cell, minZ = double(frame.originZ) * cell;
    const double spanX = double(frame.width) * cell, spanZ = double(frame.height) * cell;
    const double span = std::max(spanX, spanZ);
    const double spacingM = span / double(n - 1);
    const auto spacingCm = std::int64_t(std::llround(spacingM * 100.0));
    if (spacingCm < 100 || spacingCm > 10000000) return nullptr;
    auto field = std::make_shared<terraweave::ForestWindField>();
    field->originXcm = std::int64_t(std::llround(minX * 100.0));
    field->originZcm = std::int64_t(std::llround(minZ * 100.0));
    const auto lim = terraweave::kWorldLimitCm;
    if (field->originXcm < -lim || field->originZcm < -lim ||
        field->originXcm + spacingCm * (n - 1) > lim ||
        field->originZcm + spacingCm * (n - 1) > lim) return nullptr;
    field->spacingCm = std::int32_t(spacingCm);
    field->columns = std::uint32_t(n);
    field->rows = std::uint32_t(n);
    field->samples.resize(std::size_t(n) * std::size_t(n));
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float px = float(minX + spacingM * x), pz = float(minZ + spacingM * z);
            const auto s = frame.sample(px, pz);
            const float wx = s.valid ? s.windX : frame.meanWindX;
            const float wz = s.valid ? s.windZ : frame.meanWindZ;
            auto& out = field->samples[std::size_t(z) * std::size_t(n) + std::size_t(x)];
            out.xCmPerSec = cmPerSec(wx);
            out.zCmPerSec = cmPerSec(wz);
            out.gustPermille = gustPermille(std::hypot(wx, wz));
        }
    field->validate();
    return field;
}

inline bool sameForestWindField(const terraweave::ForestWindField* a,
                                const terraweave::ForestWindField* b) {
    if (!a || !b) return a == b;
    return a->originXcm == b->originXcm && a->originZcm == b->originZcm &&
           a->spacingCm == b->spacingCm && a->columns == b->columns &&
           a->rows == b->rows && a->samples == b->samples;
}

} // namespace aiverse::world
