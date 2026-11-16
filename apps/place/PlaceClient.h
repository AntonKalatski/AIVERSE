// Place client logic without graphics: replica client, own character predicted with the server's
// stepCharacter against Scene::staticCopy of the scene the server sent, corrections smoothed
// (net/Smoothing.h), simple statistics for the HUD and the load test. Used by the graphical client and bots.
#pragma once
#include <memory>
#include <optional>
#include "apps/place/PlaceProtocol.h"
#include "engine/net/Smoothing.h"
#include "engine/physics/Scene.h"

namespace aiverse::place {

struct PlaceClientStats {
    uint64_t reconciles = 0, corrections = 0;   // corrections: reconciles that moved the prediction > 1 cm
    float lastCorrection = 0, maxCorrection = 0;
    double sumCorrection = 0;
    uint32_t rttMs = 0;
    float snapshotsPerSecond = 0, bytesInPerSecond = 0, bytesOutPerSecond = 0;
    uint64_t spawnDenied = 0;
};

// Input frames are 6 bytes: 8 redundant copies per packet (~4 KB/s up) deliver every frame over SimNet "4g"
// (docs/replica.md § Numbers); a frame lost in every copy is skipped by the server and costs one corrected step.
inline replica::ClientOptions defaultClientOptions() { replica::ClientOptions o; o.inputRedundancy = 8; return o; }

class PlaceClient {
public:
    explicit PlaceClient(std::unique_ptr<net::Conn> connection, replica::ClientOptions options = defaultClientOptions());
    ~PlaceClient();
    // Once per fixed tick (scene tick rate): poll, reconcile on new snapshots, then send + predict `input`
    // (when ready). Returns false while not ready.
    bool update(uint64_t nowMs, const Input& input);
    void poll(uint64_t nowMs);                 // network only (e.g. between ticks)
    void advanceSmoothing(float dtMs) { smoother_.advance(dtMs); }
    void spawnBox();

    bool ready() const;                        // scene received, own character known
    bool connected() const { return client_.connected(); }
    replica::Client& replica() { return client_; }
    const replica::Client& replica() const { return client_; }
    const Schema& schema() const { return schema_; }
    replica::ObjectId own() const { return own_; }
    const physics::CharacterState& predicted() const;
    b3Vec3 drawPosition() const;               // predicted + smoothed correction
    const Json* scene() const { return scene_ ? &sceneJson_ : nullptr; }
    const std::vector<BodyLook>& staticLooks() const { return looks_; }
    physics::Scene* staticScene() { return scene_.get(); }
    const PlaceClientStats& stats() const { return stats_; }
    const physics::CharacterParams& params() const { return params_; }
    float dt() const { return scene_ ? scene_->dt() : 1.0f / 60; }

private:
    void reconcile();
    void findOwn();
    physics::CharacterState authoritative() const;

    Schema schema_;
    replica::Client client_;
    physics::CharacterParams params_;
    Json sceneJson_;
    std::unique_ptr<physics::Scene> scene_;
    std::vector<BodyLook> looks_;
    std::unique_ptr<replica::Predictor<physics::CharacterState, Input>> predictor_;
    net::CorrectionSmoother smoother_;
    replica::ObjectId own_ = 0;
    uint32_t reconciledTick_ = 0;
    uint64_t nowMs_ = 0, lastPing_ = 0, windowStart_ = 0, windowSnapshots_ = 0, windowIn_ = 0, windowOut_ = 0;
    PlaceClientStats stats_;
};
}
