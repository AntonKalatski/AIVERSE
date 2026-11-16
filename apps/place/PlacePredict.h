// Client prediction world of the place: the static and kinematic scene the server sent, plus proxies of the
// replicated bodies and characters near the own character, so stepCharacter is blocked, carried and grounded by
// the same obstacles the server's step sees (docs/replica.md § Prediction against bodies).
//
// Timing: the server applies one input frame per tick from a short buffer (PlaceServer) and reports the last one
// applied (Schema::kCharInput). Snapshot S with last applied L: input L + j meets the server's world of tick
// S + j - 1 (inputs run before the tick's physics step). rewind() puts the world back to snapshot S, each step()
// advances it one tick:
//   - kinematic bodies (movers) exactly from the tick (moverPosition), like the server;
//   - replicated bodies as local dynamic bodies restarted from the snapshot (pose, velocity, material, sleep),
//     pushed by the own character (applyPushes, push action) and by the other characters, stepped by Scene;
//   - other characters stepped with stepCharacter on a guessed input (they keep moving and turning as in the
//     snapshot), at most kMaxAheadTicks, then they hold.
// The boxes and players drawn stay the server's (interpolated); this world is only what the own character meets.
#pragma once
#include <map>
#include <memory>
#include <vector>
#include "apps/place/PlaceProtocol.h"
#include "engine/physics/Scene.h"

namespace aiverse::place {

class PredictionWorld {
public:
    static constexpr int kMaxAheadTicks = 24;   // extrapolation horizon (0.4 s at 60 Hz), then obstacles hold

    // `scene`: the join message (staticPart of the server's scene). Throws JsonError on a malformed scene.
    PredictionWorld(const Json& scene, const physics::CharacterParams& params);
    ~PredictionWorld();

    // A new snapshot arrived (client.latestTick() changed): remember the replicated obstacles.
    void record(const replica::Client& client, const Schema& schema, replica::ObjectId own);
    // Back to the newest snapshot. Obstacles within `reach` of `centre` (plus their size) get proxies.
    void rewind(b3Vec3 centre, float reach);
    // One predicted step of the own character against the world at tick(), then the world advances one tick.
    // The push action (kActionPush) is predicted too: PlaceServer::push on the stand-ins / simulated bodies.
    void step(physics::CharacterState& state, const Input& input);

    physics::Scene& scene() { return *scene_; }
    uint32_t tick() const { return tick_; }
    size_t proxies() const { return proxies_.size(); }
    size_t simulated() const;
    // Off: kinematic stand-ins extrapolated along their velocity (pushes nudge it) / characters extrapolated.
    bool simulateBodies = true;
    bool stepOthers = true;

private:
    struct Track {
        bool character = false;
        int shape = 0;
        b3Vec3 size{1, 1, 1};
        b3Vec3 pos{0, 0, 0}, vel{0, 0, 0}, spin{0, 0, 0};
        b3Quat rot = b3Quat_identity, prevRot = b3Quat_identity;
        uint32_t tick = 0, prevTick = 0;
        bool sleeping = false, seen = false;
        float extent = 0.5f, mass = 100, density = 250, friction = 0.5f;
        float yaw = 0, prevYaw = 0, yawRate = 0; // characters: replicated yaw (+ turn rate per tick), grounded, velocity
        bool grounded = false;
        b3Vec3 charVel{0, 0, 0};
    };
    struct Proxy {
        physics::BodyHandle handle = physics::kNoBody;
        bool character = false;
        int shape = 0;
        b3Vec3 size{0, 0, 0};
        b3Vec3 pos{0, 0, 0}, vel{0, 0, 0}, spin{0, 0, 0};
        b3Quat rot = b3Quat_identity;
        int ahead = 0;
        float mass = 100;
        bool dirty = true, simulated = false;
        physics::CharacterState state;           // other characters: dead-reckoned with their last input guess
        physics::CharacterInput guess;
        float yawRate = 0;
    };
    struct Kinematic { physics::BodyHandle handle; b3Vec3 start, offset; b3Quat rot; float period; bool moves; };

    void place();
    void advance();
    void push(const physics::Pushes& pushes);
    void pushAction(const physics::CharacterState& st);
    physics::BodyHandle makeProxy(const Track& t, bool simulated);

    physics::CharacterParams params_;
    std::unique_ptr<physics::Scene> scene_;
    std::vector<Kinematic> kinematic_;
    std::map<replica::ObjectId, Track> tracks_;
    std::map<replica::ObjectId, Proxy> proxies_;
    std::map<physics::BodyHandle, replica::ObjectId> byHandle_;
    uint32_t newest_ = 0, tick_ = 0;
    struct PushConfig { float speed = 6, radius = 1.2f, reach = 1.1f, maxMass = 400; uint32_t cooldown = 18; } push_;
    std::vector<uint32_t> pushTicks_;            // predicted push actions (world ticks), for the cooldown on replay
};
}
