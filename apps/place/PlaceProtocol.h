// Shared definitions of the physics sandbox place (docs/replica.md "First milestone"): the replica schema
// both sides build, the input frame, remote message channels and the scene's body looks.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "engine/data/Json.h"
#include "engine/net/replica/Replica.h"
#include "engine/physics/Character.h"

namespace aiverse::place {
namespace replica = net::replica;

// Field indices are fixed by the order in makeSchema (checked there).
struct Schema {
    replica::Schema schema;
    replica::ClassId body = 0, character = 0, game = 0;
    enum BodyField { kBodyPos, kBodyRot, kBodyShape, kBodySize, kBodyColor, kBodySleeping };
    enum CharacterField { kCharPos, kCharYaw, kCharVelocity, kCharGrounded, kCharName, kCharColor };
    enum GameField { kGameTick, kGamePlayers, kGameBodies };
    Schema();
};

enum class Shape : uint8_t { Box, Sphere, Capsule };

// Remote message channels.
enum Channel : uint16_t {
    kChannelScene = 1,      // s->c: static part of the scene JSON (prediction collisions + drawing)
    kChannelSpawnBox = 2,   // c->s: spawn a box in front of my character (rate-limited)
    kChannelSpawnDenied = 3,// s->c: the last spawn request was refused (rate limit / cap)
    kChannelPing = 4,       // both: u64 client time, echoed by the server (RTT on the HUD)
};

// One input frame: the 5-byte CharacterInput + one byte of place actions. Hostile bytes never fail.
constexpr uint8_t kActionPush = 1;
struct Input {
    physics::CharacterInput move;
    uint8_t actions = 0;
    net::Bytes pack() const;
    static Input unpack(const net::Bytes& bytes);
};

// Look of a scene body (shape, size, colour), read from the scene JSON (Scene keeps only the physics).
struct BodyLook {
    std::string id;
    physics::Motion motion = physics::Motion::Dynamic;
    Shape shape = Shape::Box;
    std::array<float, 3> size{1, 1, 1};   // box: full extents; sphere: radius x3; capsule: radius, halfHeight, radius
    uint32_t color = 0x8899AA;            // 0xRRGGBB
    bool drawable = true;                 // false for meshes (not drawn by the sample client)
};
std::vector<BodyLook> bodyLooks(const Json& scene);
uint32_t colorOf(const Json& rgb, uint32_t fallback);   // [r, g, b] in 0..1

// The scene with only static bodies (+ the "place" block): what the server sends on join.
Json staticPart(const Json& scene);

// Character tuning used by both sides (the predictor must run exactly the server's parameters).
physics::CharacterParams characterParams();

inline replica::Value vec3(float x, float y, float z) { replica::Value v; v.f = {x, y, z, 0}; return v; }
inline replica::Value quat(float x, float y, float z, float w) { replica::Value v; v.f = {x, y, z, w}; return v; }
inline replica::Value integer(int64_t i) { replica::Value v; v.i = i; return v; }
inline replica::Value text(std::string s) { replica::Value v; v.s = std::move(s); return v; }
}
