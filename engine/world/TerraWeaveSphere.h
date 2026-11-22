// TerraWeave on a sphere: the same terrain and climate model as TerraWeave.h, sampled at points on a
// sphere instead of a plane, so a cube-sphere planet gets TerraWeave relief and biomes with no seams
// (every face samples one continuous 3D field; duplicated seam corners have equal directions).
// Deterministic integer maths only (centimetres, Q16 directions); no rendering or ECS.
#pragma once

#include "TerraWeave.h"

namespace aiverse::terraweave {

struct SphereSettings {
    int64_t radiusCm = 8000;        // sphere the fields are sampled on (not the rendered radius)
    int32_t polarCooling = 450;     // temperature drop at the poles (0..1000), +Y is the polar axis
};

struct SpherePoint { int64_t x = 0, y = 0, z = 0; };   // centimetres from the centre

namespace detail {
inline uint64_t hash3(int64_t x, int64_t y, int64_t z, uint64_t seed) {
    return hash2(x, z, seed ^ mix64(static_cast<uint64_t>(y) + 0x2545f4914f6cdd1dULL));
}
// Signed Q16 trilinear value noise in centimetres: the 3D counterpart of detail::noise.
inline int64_t noise3(int64_t x, int64_t y, int64_t z, int32_t period, uint64_t seed) {
    const int64_t gx = floorDiv(x, period), gy = floorDiv(y, period), gz = floorDiv(z, period);
    const int64_t u = smooth((x - gx * period) * 65536 / period);
    const int64_t v = smooth((y - gy * period) * 65536 / period);
    const int64_t w = smooth((z - gz * period) * 65536 / period);
    const auto at = [seed](int64_t a, int64_t b, int64_t c) -> int64_t {
        return static_cast<int64_t>(hash3(a, b, c, seed) & 65535ULL) * 2 - 65535;
    };
    const int64_t y0 = lerp(lerp(at(gx, gy, gz), at(gx + 1, gy, gz), u),
                            lerp(at(gx, gy, gz + 1), at(gx + 1, gy, gz + 1), u), w);
    const int64_t y1 = lerp(lerp(at(gx, gy + 1, gz), at(gx + 1, gy + 1, gz), u),
                            lerp(at(gx, gy + 1, gz + 1), at(gx + 1, gy + 1, gz + 1), u), w);
    return lerp(y0, y1, v);
}
} // namespace detail

// A unit direction in Q16 (each component -65536..65536) to a point on the sampling sphere.
inline SpherePoint spherePoint(const SphereSettings& s, int64_t dx, int64_t dy, int64_t dz) {
    if (s.radiusCm <= 0 || s.radiusCm > kWorldLimitCm / 2)
        throw std::invalid_argument("TerraWeave sphere radius");
    return {dx * s.radiusCm / 65536, dy * s.radiusCm / 65536, dz * s.radiusCm / 65536};
}

// Height above the base, the same octave sum as heightAtCm.
inline int32_t heightAtSphere(const TerrainSettings& s, SpherePoint p) {
    if (s.basePeriodCm <= 0 || s.octaves == 0 || s.octaves > 12 || s.reliefCm < 0)
        throw std::invalid_argument("TerraWeave terrain settings");
    int64_t sum = 0, totalWeight = 0, weight = 65536;
    int32_t period = s.basePeriodCm;
    for (uint32_t i = 0; i < s.octaves; ++i) {
        sum += detail::qmul(detail::noise3(p.x, p.y, p.z, period, s.seed + i * 7919ULL), weight);
        totalWeight += weight;
        weight = std::max<int64_t>(1, weight / 2);
        period = std::max<int32_t>(1, period / 2);
    }
    return detail::clamp32(static_cast<int64_t>(s.baseHeightCm) +
                           static_cast<int64_t>(s.reliefCm) * sum / totalWeight);
}

// Climate and biome for a point whose ground height (centimetres, same scale as b.seaLevelCm) is known:
// temperature and humidity fields on the sphere, colder towards the poles and with altitude.
inline ClimateSample climateAtSphere(const SphereSettings& sph, const BiomeSettings& b, SpherePoint p, int32_t heightCm) {
    if (b.climatePeriodCm <= 0 || b.variation < 0 || sph.radiusCm <= 0)
        throw std::invalid_argument("TerraWeave biome settings");
    const int64_t lat = (p.y < 0 ? -p.y : p.y) * 1000 / sph.radiusCm;   // 0 equator .. 1000 pole
    const int64_t temperature = static_cast<int64_t>(b.baseTemperature) +
        detail::noise3(p.x, p.y, p.z, b.climatePeriodCm, b.seed) * b.variation / 65536 -
        (std::max<int64_t>(0, static_cast<int64_t>(heightCm) - b.seaLevelCm) * b.lapsePer100m / 10000) -
        lat * lat / 1000 * sph.polarCooling / 1000;
    const int64_t humidity = static_cast<int64_t>(b.baseHumidity) +
        detail::noise3(p.x, p.y, p.z, b.climatePeriodCm, b.seed ^ 0xf19c54d1ULL) * b.variation / 65536;
    ClimateSample out{detail::clamp1000(temperature), detail::clamp1000(humidity), Biome::Grassland};
    out.biome = classifyBiome(out, heightCm, b.seaLevelCm);
    return out;
}

} // namespace aiverse::terraweave
