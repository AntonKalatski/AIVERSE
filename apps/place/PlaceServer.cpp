#include "apps/place/PlaceServer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace aiverse::place {
namespace {
using physics::BodyHandle;
using physics::kNoBody;
[[noreturn]] void fail(const std::string& m) { throw JsonError("place: " + m); }
float num(const Json& j, const char* key, float def, float lo, float hi) {
    if (!j.has(key)) return def;
    double v = j[key].isNumber() ? j[key].toDouble() : NAN;
    if (!std::isfinite(v) || v < lo || v > hi) fail(std::string(key) + " must be a number in [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    return float(v);
}
b3Vec3 vec(const Json& j, const std::string& what) {
    if (!j.isArray() || j.size() != 3) fail(what + " must be an array of three numbers");
    float o[3];
    for (size_t i = 0; i < 3; ++i) {
        double v = j[i].isNumber() ? j[i].toDouble() : NAN;
        if (!std::isfinite(v) || std::fabs(v) > 1000) fail(what + " out of range");
        o[i] = float(v);
    }
    return {o[0], o[1], o[2]};
}
b3Vec3 add(b3Vec3 a, b3Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
b3Vec3 mul(b3Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
replica::Value quatValue(b3Quat q) { return quat(q.v.x, q.v.y, q.v.z, q.s); }
replica::Value yawValue(float yaw) { return quat(0, std::sin(yaw / 2), 0, std::cos(yaw / 2)); }
constexpr uint32_t kPalette[] = {0xE8574A, 0x4AA3E8, 0x6CCB5F, 0xF2C14E, 0xB57EDC, 0xF28E4E, 0x4ED6C9, 0xE86FA8};
}

PlaceConfig PlaceConfig::read(const Json& scene) {
    PlaceConfig c;
    const Json& p = scene["place"];
    if (p.isNull()) return c;
    if (!p.isObject()) fail("\"place\" must be an object");
    if (p.has("spawns")) {
        const auto& s = p["spawns"];
        if (!s.isArray() || s.size() == 0 || s.size() > 256) fail("spawns must be an array of 1..256 points");
        c.spawns.clear();
        for (size_t i = 0; i < s.size(); ++i) c.spawns.push_back(vec(s[i], "spawns[" + std::to_string(i) + "]"));
    }
    c.relevance = num(p, "relevance", c.relevance, 1, 10000);
    c.killY = num(p, "killY", c.killY, -10000, 0);
    const auto& box = p["spawnBox"];
    if (box.isObject()) {
        if (box.has("size")) c.boxSize = vec(box["size"], "spawnBox.size");
        if (c.boxSize.x < 0.05f || c.boxSize.y < 0.05f || c.boxSize.z < 0.05f || c.boxSize.x > 10 || c.boxSize.y > 10 || c.boxSize.z > 10)
            fail("spawnBox.size must be 0.05..10");
        c.spawnPerSecond = num(box, "perSecond", c.spawnPerSecond, 0, 60);
        c.spawnBurst = num(box, "burst", c.spawnBurst, 1, 100);
        c.maxSpawnedPerPlayer = int(num(box, "maxPerPlayer", float(c.maxSpawnedPerPlayer), 0, 1000));
        c.maxBodies = int(num(box, "maxBodies", float(c.maxBodies), 1, 65000));
    }
    const auto& push = p["push"];
    if (push.isObject()) {
        c.pushSpeed = num(push, "speed", c.pushSpeed, 0, 50);
        c.pushRadius = num(push, "radius", c.pushRadius, 0.1f, 10);
        c.pushReach = num(push, "reach", c.pushReach, 0, 10);
        c.pushMaxMass = num(push, "maxMass", c.pushMaxMass, 1, 100000);
        c.pushCooldownTicks = int(num(push, "cooldownTicks", float(c.pushCooldownTicks), 1, 600));
    }
    const auto& movers = p["movers"];
    if (!movers.isNull() && (!movers.isArray() || movers.size() > 256)) fail("movers must be an array of at most 256 objects");
    for (size_t i = 0; i < movers.size(); ++i) {
        Mover m;
        m.body = movers[i].getStr("body");
        if (m.body.empty()) fail("movers[" + std::to_string(i) + "].body must name a kinematic body");
        m.offset = vec(movers[i]["offset"], "movers[" + std::to_string(i) + "].offset");
        m.period = num(movers[i], "period", m.period, 0.5f, 600);
        c.movers.push_back(m);
    }
    return c;
}

PlaceServer::PlaceServer(const Json& scene, replica::ServerOptions options, int workers)
    : config_(PlaceConfig::read(scene)), params_(characterParams()), scene_(physics::Scene::fromJson(scene, physics::Scene::Load::All, workers)),
      server_(schema_.schema, [&] { options.tickRate = scene_->tickRate(); options.defaultRadius = config_.relevance; return options; }()) {
    std::string text = staticPart(scene).dump(0);
    staticScene_.assign(text.begin(), text.end());
    int64_t kinematic = 0;
    for (const auto& look : bodyLooks(scene)) {
        if (look.motion == physics::Motion::Static) continue;
        if (look.motion == physics::Motion::Kinematic) ++kinematic;   // numbered in JSON order, as the client does
        BodyHandle h = scene_->find(look.id);
        if (h == kNoBody) continue;
        replica::ObjectId id = track(h, look, 0);
        if (look.motion == physics::Motion::Kinematic) server_.set(id, Schema::kBodyKinematic, integer(kinematic));
    }
    for (const auto& m : config_.movers) {
        BodyHandle h = scene_->find(m.body);
        if (h == kNoBody || scene_->motion(h) != physics::Motion::Kinematic) fail("mover body '" + m.body + "' must be a kinematic body");
        movers_.push_back({h, scene_->position(h), m.offset, scene_->rotation(h), m.period});
    }
    game_ = server_.create(schema_.game);
    server_.onJoin = [this](uint8_t slot) { join(slot); };
    server_.onLeave = [this](uint8_t slot) { leave(slot); };
    server_.onInput = [this](uint8_t slot, uint32_t seq, const net::Bytes& b) { input(slot, seq, b); };
    server_.onMessage = [this](uint8_t slot, uint16_t ch, const net::Bytes& b) { message(slot, ch, b); };
}

PlaceServer::~PlaceServer() = default;

replica::ObjectId PlaceServer::track(BodyHandle h, const BodyLook& look, uint8_t spawnedBy) {
    Rep r;
    r.handle = h;
    r.id = server_.create(schema_.body);
    r.start = scene_->position(h);
    r.startRot = scene_->rotation(h);
    r.spawnedBy = spawnedBy;
    server_.set(r.id, Schema::kBodyPos, vec3(r.start.x, r.start.y, r.start.z));
    server_.set(r.id, Schema::kBodyRot, quatValue(r.startRot));
    server_.set(r.id, Schema::kBodyShape, integer(int64_t(look.shape)));
    server_.set(r.id, Schema::kBodySize, vec3(look.size[0], look.size[1], look.size[2]));
    server_.set(r.id, Schema::kBodyColor, integer(look.color));
    r.awake = scene_->awake(h);
    server_.set(r.id, Schema::kBodySleeping, integer(!r.awake));
    b3ShapeId shape;
    if (b3Body_GetShapes(scene_->native(h), &shape, 1) == 1) {
        replica::Value density, friction;
        density.f[0] = b3Shape_GetDensity(shape);
        friction.f[0] = b3Shape_GetFriction(shape);
        server_.set(r.id, Schema::kBodyDensity, density);
        server_.set(r.id, Schema::kBodyFriction, friction);
    }
    repOf_[h] = reps_.size();
    reps_.push_back(r);
    return r.id;
}

void PlaceServer::untrack(BodyHandle h) {
    auto it = repOf_.find(h);
    if (it == repOf_.end()) return;
    size_t i = it->second;
    server_.destroy(reps_[i].id);
    scene_->destroy(h);
    repOf_.erase(it);
    reps_.erase(reps_.begin() + long(i));
    for (size_t k = i; k < reps_.size(); ++k) repOf_[reps_[k].handle] = k;
}

std::vector<replica::ObjectId> PlaceServer::bodies() const {
    std::vector<replica::ObjectId> out;
    for (const auto& r : reps_) out.push_back(r.id);
    return out;
}
BodyHandle PlaceServer::bodyHandle(replica::ObjectId id) const {
    for (const auto& r : reps_) if (r.id == id) return r.handle;
    return kNoBody;
}
replica::ObjectId PlaceServer::characterOf(uint8_t slot) const {
    auto it = players_.find(slot);
    return it == players_.end() ? 0 : it->second.object;
}
const physics::CharacterState* PlaceServer::characterState(uint8_t slot) const {
    auto it = players_.find(slot);
    return it == players_.end() ? nullptr : &it->second.state;
}

void PlaceServer::respawn(Player& p, uint8_t slot) {
    b3Vec3 base = config_.spawns[size_t(slot) % config_.spawns.size()];
    // Spread players that share a spawn point over a small grid so the capsules do not start inside each other.
    int k = int(slot / config_.spawns.size());
    p.state = {};
    p.state.position = add(base, {float(k % 4) * 1.2f - 1.8f, 0, float((k / 4) % 4) * 1.2f - 1.8f});
    scene_->moveCharacter(p.proxy, p.state, true);
}

void PlaceServer::join(uint8_t slot) {
    Player p;
    p.proxy = scene_->createCharacter(params_, {0, 0, 0}, slot);
    p.object = server_.create(schema_.character);
    p.spawnTokens = config_.spawnBurst;
    p.lastInputTick = tick_;
    respawn(p, slot);
    server_.setOwner(p.object, slot);
    server_.set(p.object, Schema::kCharName, text("Player " + std::to_string(slot)));
    server_.set(p.object, Schema::kCharColor, integer(kPalette[slot % (sizeof(kPalette) / sizeof(kPalette[0]))]));
    players_[slot] = std::move(p);
    server_.send(slot, kChannelScene, staticScene_);
}

void PlaceServer::leave(uint8_t slot) {
    auto it = players_.find(slot);
    if (it == players_.end()) return;
    server_.destroy(it->second.object);
    scene_->destroy(it->second.proxy);
    players_.erase(it);   // boxes it spawned stay in the world
}

void PlaceServer::step(Player& p, const physics::CharacterInput& in) {
    physics::Pushes pushes;
    physics::stepCharacter(scene_->world(), params_, p.state, in, scene_->dt(), &pushes, p.proxy);
    scene_->applyPushes(pushes);
}

namespace {
constexpr size_t kInputBuffer = 2;     // frames held before applying (absorbs ~2 ticks of arrival jitter)
constexpr size_t kInputCatchUp = 6;    // above this many queued, two frames per tick
constexpr size_t kInputQueueMax = 32;
}

void PlaceServer::input(uint8_t slot, uint32_t seq, const net::Bytes& frame) {
    auto it = players_.find(slot);
    if (it == players_.end()) return;
    Player& p = it->second;
    if (frame.size() != physics::CharacterInput::kPackedSize + 1) ++stats_.badMessages;   // still unpacked safely
    p.inputs.push_back({seq, Input::unpack(frame)});
    if (p.inputs.size() > kInputQueueMax) p.inputs.pop_front();
}

void PlaceServer::apply(Player& p, const Input& in) {
    ++stats_.inputs;
    step(p, in.move);
    p.lastInputTick = tick_;
    if ((in.actions & kActionPush) && tick_ >= p.pushReadyTick) {
        p.pushReadyTick = tick_ + uint64_t(config_.pushCooldownTicks);
        push(p);
    }
}

void PlaceServer::push(Player& p) {
    ++stats_.pushes;
    float s = std::sin(p.state.yaw), c = std::cos(p.state.yaw);
    b3Vec3 fwd{-s, 0, -c};
    b3Vec3 centre = add(add(p.state.position, mul(fwd, config_.pushReach)), {0, params_.height * 0.5f, 0});
    physics::QueryFilter f;
    f.mask = physics::kCategoryDynamic;
    b3Vec3 dir{fwd.x * 0.94f, 0.34f, fwd.z * 0.94f};
    for (BodyHandle h : scene_->overlapSphere(centre, config_.pushRadius, f)) {
        float mass = b3Body_GetMass(scene_->native(h));
        scene_->applyImpulse(h, mul(dir, std::min(mass, config_.pushMaxMass) * config_.pushSpeed));
        ++stats_.pushedBodies;
    }
}

void PlaceServer::spawnBox(uint8_t slot, Player& p) {
    if (p.spawnTokens < 1 || int(reps_.size()) >= config_.maxBodies || config_.maxSpawnedPerPlayer == 0) {
        ++stats_.spawnDenied;
        server_.send(slot, kChannelSpawnDenied, {});
        return;
    }
    if (int(p.spawned.size()) >= config_.maxSpawnedPerPlayer) {   // the oldest one of this player makes room
        untrack(p.spawned.front());
        p.spawned.pop_front();
    }
    float s = std::sin(p.state.yaw), c = std::cos(p.state.yaw);
    physics::BodyDesc d;
    d.shape = physics::ShapeKind::Box;
    d.size = config_.boxSize;
    d.density = 200;
    d.position = add(p.state.position, {-s * 1.6f, params_.height + 0.8f, -c * 1.6f});
    BodyHandle h = scene_->create(d);
    if (h == kNoBody) { ++stats_.spawnDenied; server_.send(slot, kChannelSpawnDenied, {}); return; }
    p.spawnTokens -= 1;
    BodyLook look;
    look.shape = Shape::Box;
    look.size = {d.size.x, d.size.y, d.size.z};
    look.color = kPalette[(colorSeed_++ * 5) % (sizeof(kPalette) / sizeof(kPalette[0]))];
    track(h, look, slot);
    p.spawned.push_back(h);
    ++stats_.spawned;
}

void PlaceServer::message(uint8_t slot, uint16_t channel, const net::Bytes& payload) {
    auto it = players_.find(slot);
    if (it == players_.end()) return;
    if (channel == kChannelSpawnBox && payload.empty()) spawnBox(slot, it->second);
    else if (channel == kChannelPing && payload.size() == 8) server_.send(slot, kChannelPing, payload);
    else ++stats_.badMessages;
}

void PlaceServer::syncBodies() {
    std::vector<BodyHandle> fallen;
    for (auto& r : reps_) {
        bool awake = scene_->awake(r.handle);
        if (!awake && !r.awake) continue;              // sleeping bodies cost nothing
        b3Vec3 pos = scene_->position(r.handle);
        if (pos.y < config_.killY) {
            if (r.spawnedBy) { fallen.push_back(r.handle); continue; }
            scene_->setTransform(r.handle, r.start, r.startRot);
            scene_->setLinearVelocity(r.handle, {0, 0, 0});
            scene_->setAngularVelocity(r.handle, {0, 0, 0});
            pos = r.start;
        }
        server_.set(r.id, Schema::kBodyPos, vec3(pos.x, pos.y, pos.z));
        server_.set(r.id, Schema::kBodyRot, quatValue(scene_->rotation(r.handle)));
        server_.set(r.id, Schema::kBodySleeping, integer(!awake));
        b3Vec3 v = awake ? scene_->linearVelocity(r.handle) : b3Vec3{0, 0, 0};
        server_.set(r.id, Schema::kBodyVelocity, vec3(v.x, v.y, v.z));
        r.awake = awake;
    }
    for (BodyHandle h : fallen) {
        for (auto& [slot, p] : players_) p.spawned.erase(std::remove(p.spawned.begin(), p.spawned.end(), h), p.spawned.end());
        untrack(h);
    }
}

void PlaceServer::syncCharacters() {
    for (auto& [slot, p] : players_) {
        const auto& st = p.state;
        server_.set(p.object, Schema::kCharPos, vec3(st.position.x, st.position.y, st.position.z));
        server_.set(p.object, Schema::kCharYaw, yawValue(st.yaw));
        server_.set(p.object, Schema::kCharVelocity, vec3(st.velocity.x, st.velocity.y, st.velocity.z));
        server_.set(p.object, Schema::kCharGrounded, integer(st.grounded));
        server_.set(p.object, Schema::kCharInput, integer(int64_t(p.applied & 0xFFFFu)));
        server_.setViewers(slot, {{st.position.x, st.position.y, st.position.z}});
    }
}

void PlaceServer::update(uint64_t nowMs) {
    auto t0 = std::chrono::steady_clock::now();
    server_.poll(nowMs);                 // input frames (onInput, queued per player), messages
    const float dt = scene_->dt();
    for (auto& [slot, p] : players_) {
        if (p.inputs.empty()) { if (!p.buffering) ++stats_.inputUnderruns; p.buffering = true; }
        else if (p.buffering && p.inputs.size() >= kInputBuffer) p.buffering = false;
        if (p.buffering) continue;
        size_t n = p.inputs.size() > kInputCatchUp ? 2 : 1;
        if (n == 2) ++stats_.inputCatchUps;
        for (size_t k = 0; k < n && !p.inputs.empty(); ++k) {
            p.applied = p.inputs.front().first;
            Input in = p.inputs.front().second;
            p.inputs.pop_front();
            apply(p, in);
        }
    }
    for (auto& [slot, p] : players_) {
        // A client that stopped sending still falls and rides platforms (its prediction reconciles later).
        if (tick_ - p.lastInputTick >= 60) { step(p, {}); ++stats_.idleSteps; }
        if (p.state.position.y < config_.killY) respawn(p, slot);
        scene_->moveCharacter(p.proxy, p.state);
        p.spawnTokens = std::min<double>(config_.spawnBurst, p.spawnTokens + config_.spawnPerSecond * dt);
    }
    for (const auto& m : movers_) {
        scene_->moveKinematic(m.handle, moverPosition(m.start, m.offset, m.period, tick_ + 1, dt), m.rot);
    }
    scene_->step();
    ++tick_;
    syncBodies();
    syncCharacters();
    if (tick_ % 30 == 0 || tick_ == 1) {
        server_.set(game_, Schema::kGameTick, integer(int64_t(tick_)));
        server_.set(game_, Schema::kGamePlayers, integer(int64_t(players_.size())));
        server_.set(game_, Schema::kGameBodies, integer(int64_t(reps_.size())));
    }
    server_.tick();
    server_.broadcast(nowMs);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++stats_.ticks;
    stats_.tickMsSum += ms;
    stats_.tickMsWorst = std::max(stats_.tickMsWorst, ms);
}
}
