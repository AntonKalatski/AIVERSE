// Universal ECS camera authoring components; no GPU handles, float transforms,
// or genre-specific dependencies. Parent/LocalTransform/WorldTransform are owned
// by the existing SceneHierarchy. The renderer reads the derived WorldTransform
// *after* the hierarchy update barrier.
#pragma once
#include "CommonComponents.h"
#include <cstdint>

namespace aiverse {

enum class CameraProjection : uint8_t { Perspective, Orthographic };

// Scene-authored view settings. This component may be persisted through
// PersistentEcs if a game registers a stable schema/codec for it.
struct CameraComponent {
    CameraProjection projection = CameraProjection::Perspective;
    Fixed verticalFov = Fixed::fromRatio(3, 4); // radians, not degrees
    Fixed orthographicHalfHeight = 10_fx;
    Fixed nearPlane = Fixed::fromRatio(1, 10);
    Fixed farPlane = 1000_fx;
    uint32_t visibilityMask = UINT32_MAX;
    int32_t priority = 0;
    bool enabled = true;
    constexpr bool operator==(const CameraComponent&) const = default;
};

// Stable render-target *asset key*, not a gfx::TextureHandle. Zero denotes the
// screen output and a nonzero key identifies a host-managed texture asset.
enum class CameraOutputKind : uint8_t { Screen, Texture };
struct CameraOutputComponent {
    CameraOutputKind kind = CameraOutputKind::Screen;
    uint64_t targetAssetKey = 0;
    uint16_t width = 0, height = 0; // zero = use host/output dimensions
    constexpr bool operator==(const CameraOutputComponent&) const = default;
};

enum class CameraUpdateMode : uint8_t {
    EveryFrame, EveryNFrames, FixedRate, OnDirty, Manual
};
struct CameraUpdateComponent {
    CameraUpdateMode mode = CameraUpdateMode::EveryFrame;
    uint16_t everyNFrames = 1;
    uint16_t rateHz = 30;
    constexpr bool operator==(const CameraUpdateComponent&) const = default;
};

} // namespace aiverse
