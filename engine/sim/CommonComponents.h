// Components shared by all gameplay modules.
#pragma once
#include <cstdint>
#include <array>
#include "../core/ECS.h"
#include "../core/Fixed.h"

namespace aiverse {

struct Transform {
    Vec3 pos{};
    Vec3 prevPos{};   // position at the previous tick, for render interpolation
    Fixed yaw{};      // facing, in "turns" [0,1) to avoid trig in the sim
};

struct Parent {Entity value=kNullEntity;};
struct Rotation {
    Fixed x{},y{},z{},w=1_fx;
    constexpr bool operator==(const Rotation&)const=default;
};
struct LocalTransform {
    Vec3 position{};
    Rotation rotation{};
    Vec3 scale{1_fx,1_fx,1_fx};
    constexpr bool operator==(const LocalTransform&)const=default;
};
struct AffineTransform {
    // Row-major 3x4 affine matrix; authoritative for full nested TRS,
    // including shear induced by rotated children of non-uniformly scaled
    // parents. Translation is column 3.
    std::array<Fixed,12> m{
        1_fx,Fixed{},Fixed{},Fixed{},
        Fixed{},1_fx,Fixed{},Fixed{},
        Fixed{},Fixed{},1_fx,Fixed{}
    };
    constexpr Fixed at(size_t row,size_t col)const{return m[row*4+col];}
    constexpr bool operator==(const AffineTransform&)const=default;
};
struct WorldTransform {
    Vec3 position{};
    Rotation rotation{};
    Vec3 scale{1_fx,1_fx,1_fx};
    AffineTransform affine{}; // use this for rendering and spatial queries
    constexpr bool operator==(const WorldTransform&)const=default;
};

struct Owner {
    uint8_t player = 0; // 0 = neutral
};

} // namespace aiverse
