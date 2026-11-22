// AIVERSE LENS: authoring components for cinematic and procedural cameras.
// Fixed-point ECS data; no graphics handles or presentation clocks stored here.
#pragma once
#include "CommonComponents.h"
#include <array>
#include <cstdint>

namespace aiverse {
// Chase / FirstPerson (appended) run the same math as the view cameras (render/CameraBehavior.h):
//   Chase       - behind the target's heading: distance, pitch, offset.y = height, offset.z = lead ahead,
//                 yaw = turn around the heading (radians);
//   FirstPerson - eyes at target + offset (offset turns with the target), looking along the target's +Z,
//                 yaw / pitch added on top.
enum class CameraRigMode : uint8_t { Static, LookAt, Follow, Orbit, Rail, Chase, FirstPerson };
struct CameraRigComponent {
    CameraRigMode mode=CameraRigMode::Static;
    Entity target=kNullEntity;                // stable generation checked target
    Vec3 offset{0_fx,2_fx,-5_fx};            // world-space follow offset
    Fixed distance=5_fx, yaw=0_fx, pitch=0_fx; // angles in radians
    Fixed smoothing=8_fx;                    // presentation damping, 0 = instant
    bool trackTarget=true;
    bool enabled=true;
    constexpr bool operator==(const CameraRigComponent&) const = default;
};
// Optional modifiers of the Follow rig mode (the shared follow body, render/CameraBehavior.h): when present, a
// Follow rig frames its target like the game follow camera (angle, distance, smoothing, velocity lead, speed zoom,
// bounds) instead of a fixed offset. Angles in degrees; distances in world units. Scene JSON: "follow": {"preset"...}.
struct CameraFollowComponent {
    Fixed pitch=58_fx, yaw=0_fx;
    Fixed distance=20_fx, minDistance=10_fx, maxDistance=40_fx;
    Fixed smoothing=6_fx, lookAheadSmoothing=3_fx;
    Fixed velocityLookAhead=0_fx, maxLookAhead=0_fx;          // seconds of velocity ahead; cap (0: none)
    Fixed speedFrom=0_fx, speedTo=0_fx, speedDistance=0_fx, speedSmoothing=Fixed::fromRatio(3,2);
    Fixed fov=0_fx;                                           // degrees, 0 = keep the camera's
    bool bounds=false;                                        // keep the focus inside [minX..maxX] x [minZ..maxZ]
    Fixed minX=0_fx, minZ=0_fx, maxX=0_fx, maxZ=0_fx, margin=0_fx;
    constexpr bool operator==(const CameraFollowComponent&) const = default;
};
enum class CameraRailInterpolation : uint8_t { Linear, CatmullRom };
enum class CameraRailEase : uint8_t { None, InOut };
struct CameraRailComponent {
    std::array<Vec3,8> points{};             // local view-world points
    uint8_t count=0;
    Fixed duration=5_fx;
    bool loop=false;
    // Appended (aggregate order preserved; defaults = the original linear rail).
    // CatmullRom: centripetal spline through every point. constantSpeed:
    // arc-length timing. InOut ease: smoothstep start/stop over the duration.
    CameraRailInterpolation interpolation=CameraRailInterpolation::Linear;
    bool constantSpeed=false;
    CameraRailEase ease=CameraRailEase::None;
    constexpr bool operator==(const CameraRailComponent&) const = default;
};
enum class CameraShakeStyle : uint8_t { Sine, Noise };
struct CameraShakeComponent {
    Fixed positionAmplitude=0_fx, rotationAmplitude=0_fx; // m / radians
    Fixed frequency=12_fx;
    Fixed startTime=0_fx, duration=0_fx;     // presentation seconds
    // Appended. Noise: smooth seeded noise on 3 position + 3 rotation axes
    // (pitch/yaw/roll). fadeExponent shapes (1-age/duration)^k; 1 = linear.
    CameraShakeStyle style=CameraShakeStyle::Sine;
    uint32_t seed=0;
    Fixed fadeExponent=1_fx;
    constexpr bool operator==(const CameraShakeComponent&) const = default;
};
// These are physical-lens metadata. Sensor/focal length determine the vertical
// FOV; f-number, shutter and ISO feed per-camera post processing later.
struct CameraLensComponent {
    Fixed sensorHeightMm=24_fx, focalLengthMm=50_fx;
    Fixed fNumber=Fixed::fromRatio(28,10);
    Fixed focusDistance=10_fx;
    Fixed shutterSeconds=Fixed::fromRatio(1,60);
    Fixed iso=100_fx;
    bool physicalFov=false;
    // Appended (aggregate order preserved). Opt-in presentation drives:
    // exposure = stops relative to the default lens (f/2.8, 1/60 s, ISO 100)
    // plus compensation; depth of field = thin-lens near/far focus limits.
    // circleOfConfusionMm 0 = automatic (sensor height / 800).
    bool driveExposure=false, driveDepthOfField=false;
    Fixed exposureCompensation=0_fx;
    Fixed circleOfConfusionMm=0_fx;
    constexpr bool operator==(const CameraLensComponent&) const = default;
};
} // namespace aiverse
