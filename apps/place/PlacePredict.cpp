#include "apps/place/PlacePredict.h"
#include <algorithm>
#include <cmath>
#include "apps/place/PlaceServer.h"
#include "engine/physics/Character.h"

namespace aiverse::place {
namespace {
using physics::BodyHandle;
using physics::kNoBody;
b3Vec3 add(b3Vec3 a, b3Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
b3Vec3 sub(b3Vec3 a, b3Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
b3Vec3 mul(b3Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(b3Vec3 a, b3Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
b3Vec3 vec(const replica::Value& v) { return {v.f[0], v.f[1], v.f[2]}; }
b3Quat quat(const replica::Value& v) { return {{v.f[0], v.f[1], v.f[2]}, v.f[3]}; }
b3Quat normalized(b3Quat q) {
    float l = std::sqrt(dot(q.v, q.v) + q.s * q.s);
    return l > 1e-6f ? b3Quat{mul(q.v, 1 / l), q.s / l} : b3Quat_identity;
}
// Angular velocity turning `from` into `to` over `seconds`.
b3Vec3 spinBetween(b3Quat from, b3Quat to, float seconds) {
    b3Quat d = b3MulQuat(to, b3Conjugate(from));
    if (d.s < 0) d = {mul(d.v, -1), -d.s};
    float s = std::sqrt(dot(d.v, d.v));
    if (s < 1e-6f || seconds <= 0) return {0, 0, 0};
    float angle = 2 * std::atan2(s, d.s);
    return mul(d.v, angle / (s * seconds));
}
b3Quat integrate(b3Quat q, b3Vec3 w, float dt) {
    float l = std::sqrt(dot(w, w));
    if (l * dt < 1e-7f) return q;
    return normalized(b3MulQuat(b3MakeQuatFromAxisAngle(mul(w, 1 / l), l * dt), q));
}
bool finite(b3Vec3 v, float limit) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit; }
}

PredictionWorld::PredictionWorld(const Json& scene, const physics::CharacterParams& params)
    : params_(params), scene_(physics::Scene::fromJson(scene, physics::Scene::Load::All, 1)) {
    // Kinematic bodies, numbered in JSON order like the server's kBodyKinematic field.
    PlaceConfig config = PlaceConfig::read(scene);
    const auto& bodies = scene["bodies"];
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].getStr("motion", "dynamic") != "kinematic") continue;
        Kinematic k{scene_->find(bodies[i].getStr("id")), {0, 0, 0}, {0, 0, 0}, b3Quat_identity, 1, false};
        if (k.handle != kNoBody) { k.start = scene_->position(k.handle); k.rot = scene_->rotation(k.handle); }
        for (const auto& m : config.movers)
            if (m.body == bodies[i].getStr("id")) { k.offset = m.offset; k.period = m.period; k.moves = true; }
        kinematic_.push_back(k);
    }
    push_ = {config.pushSpeed, config.pushRadius, config.pushReach, config.pushMaxMass, uint32_t(config.pushCooldownTicks)};
}

PredictionWorld::~PredictionWorld() = default;

void PredictionWorld::record(const replica::Client& c, const Schema& schema, replica::ObjectId own) {
    uint32_t tick = c.latestTick();
    for (auto& [id, t] : tracks_) t.seen = false;
    const float dt = scene_->dt();
    for (auto id : c.objects()) {
        auto cls = c.classOf(id);
        const bool character = cls == schema.character;
        if ((!character && cls != schema.body) || id == own) continue;
        if (!character && c.latest(id, Schema::kBodyKinematic).i != 0) continue;   // moved locally, exactly
        Track& t = tracks_[id];
        bool fresh = t.tick == 0;
        b3Vec3 pos = vec(c.latest(id, character ? int(Schema::kCharPos) : int(Schema::kBodyPos)));
        b3Quat rot = character ? b3Quat_identity : normalized(quat(c.latest(id, Schema::kBodyRot)));
        if (!finite(pos, 1e5f)) continue;
        t.seen = true;
        const bool advanced = tick != t.tick;
        if (advanced) { t.prevRot = fresh ? rot : t.rot; t.prevTick = fresh ? tick : t.tick; t.prevYaw = t.yaw; }
        t.pos = pos;
        t.rot = rot;
        t.tick = tick;
        t.character = character;
        if (character) {
            t.shape = int(Shape::Capsule);
            t.extent = params_.height;
            b3Vec3 v = vec(c.latest(id, Schema::kCharVelocity));
            t.grounded = c.latest(id, Schema::kCharGrounded).i != 0;
            t.vel = t.grounded ? b3Vec3{v.x, 0, v.z} : v;
            t.charVel = v;
            auto q = c.latest(id, Schema::kCharYaw).f;
            t.yaw = 2 * std::atan2(q[1], q[3]);
            if (fresh) t.prevYaw = t.yaw;
            uint32_t span = t.tick - t.prevTick;
            float dy = std::remainder(t.yaw - t.prevYaw, 6.2831853f);
            t.yawRate = span > 0 && span <= 15 ? std::clamp(dy / float(span), -0.2f, 0.2f) : 0.0f;
            t.spin = {0, 0, 0};
            continue;
        }
        t.shape = int(c.latest(id, Schema::kBodyShape).i);
        t.size = vec(c.latest(id, Schema::kBodySize));
        t.sleeping = c.latest(id, Schema::kBodySleeping).i != 0;
        t.density = std::clamp(c.latest(id, Schema::kBodyDensity).f[0], 0.01f, 100000.0f);
        t.friction = std::clamp(c.latest(id, Schema::kBodyFriction).f[0], 0.0f, 2.0f);
        const b3Vec3& s = t.size;
        float volume = t.shape == int(Shape::Box) ? s.x * s.y * s.z : t.shape == int(Shape::Sphere) ? 4.18879f * s.x * s.x * s.x
                                                                       : 3.14159f * s.x * s.x * (2 * s.y + 1.33333f * s.x);
        t.mass = std::max(0.01f, volume * t.density);
        t.extent = t.shape == int(Shape::Box) ? 0.5f * std::sqrt(dot(s, s)) : t.shape == int(Shape::Sphere) ? s.x : s.y + s.x;
        uint32_t span = t.tick - t.prevTick;
        if (t.sleeping || span == 0 || span > 30) { t.vel = {0, 0, 0}; t.spin = {0, 0, 0}; }
        else {
            float sec = float(span) * dt;
            t.vel = vec(c.latest(id, Schema::kBodyVelocity));   // the server's; differences of 1 cm positions are too noisy
            t.spin = spinBetween(t.prevRot, t.rot, sec);
            if (!finite(t.vel, 100) || !finite(t.spin, 100)) { t.vel = {0, 0, 0}; t.spin = {0, 0, 0}; }
        }
    }
    for (auto it = tracks_.begin(); it != tracks_.end();) it = it->second.seen ? std::next(it) : tracks_.erase(it);
    newest_ = tick;
}

size_t PredictionWorld::simulated() const {
    size_t n = 0;
    for (const auto& [id, p] : proxies_) n += p.simulated;
    return n;
}

BodyHandle PredictionWorld::makeProxy(const Track& t, bool simulated) {
    if (t.character) return scene_->createCharacter(params_, t.pos);
    physics::BodyDesc d;
    d.category = physics::kCategoryDynamic;       // the controller treats a stand-in as the dynamic body it is
    if (simulated) {
        d.motion = physics::Motion::Dynamic;
        d.density = t.density;
        d.friction = t.friction;
        d.linearVelocity = t.vel;
        d.angularVelocity = t.spin;
        d.sleep = false;
    } else {
        d.motion = physics::Motion::Kinematic;
        d.mask = physics::kCategoryQuery | physics::kCategoryDynamic;   // seen by the controller, blocks simulated ones
    }
    d.position = t.pos;
    d.rotation = t.rot;
    if (t.shape == int(Shape::Sphere)) { d.shape = physics::ShapeKind::Sphere; d.radius = t.size.x; }
    else if (t.shape == int(Shape::Capsule)) { d.shape = physics::ShapeKind::Capsule; d.radius = t.size.x; d.halfHeight = t.size.y; }
    else { d.shape = physics::ShapeKind::Box; d.size = t.size; }
    return scene_->create(d);
}

void PredictionWorld::rewind(b3Vec3 centre, float reach) {
    tick_ = newest_;
    pushTicks_.erase(std::remove_if(pushTicks_.begin(), pushTicks_.end(), [&](uint32_t t) { return int32_t(t - newest_) >= 0 || newest_ - t > 600; }),
                     pushTicks_.end());
    auto destroyProxy = [&](std::map<replica::ObjectId, Proxy>::iterator it) {
        byHandle_.erase(it->second.handle);
        scene_->destroy(it->second.handle);
        return proxies_.erase(it);
    };
    for (auto it = proxies_.begin(); it != proxies_.end();) {
        auto t = tracks_.find(it->first);
        bool drop = t == tracks_.end() || t->second.character != it->second.character || t->second.shape != it->second.shape ||
                    dot(sub(t->second.size, it->second.size), sub(t->second.size, it->second.size)) > 1e-6f;
        if (!drop) {
            b3Vec3 d = sub(t->second.pos, centre);
            drop = std::sqrt(dot(d, d)) - t->second.extent > reach + 2;   // hysteresis: kept 2 m past the reach
        }
        it = drop ? destroyProxy(it) : std::next(it);
    }
    for (const auto& [id, t] : tracks_) {
        auto it = proxies_.find(id);
        if (it == proxies_.end()) {
            b3Vec3 d = sub(t.pos, centre);
            if (std::sqrt(dot(d, d)) - t.extent > reach) continue;
            Proxy p;
            p.character = t.character;
            p.shape = t.shape;
            p.size = t.size;
            it = proxies_.emplace(id, p).first;
        }
        Proxy& p = it->second;
        bool sim = simulateBodies && !t.character;
        if (p.handle == kNoBody || sim != p.simulated) {
            if (p.handle != kNoBody) { byHandle_.erase(p.handle); scene_->destroy(p.handle); }
            p.handle = makeProxy(t, sim);
            p.simulated = sim;
            if (p.handle == kNoBody) { proxies_.erase(it); continue; }
            byHandle_[p.handle] = id;
        }
        p.pos = t.pos;
        p.rot = t.rot;
        p.vel = t.vel;
        p.spin = t.spin;
        p.mass = t.mass;
        p.ahead = 0;
        p.dirty = true;
        if (p.character) {
            // Their next inputs are unknown: guess they keep walking the way they move (relative to their yaw).
            p.state = {};
            p.state.position = t.pos;
            p.state.velocity = t.charVel;
            p.state.yaw = t.yaw;
            p.state.grounded = t.grounded;
            float sp = std::sqrt(t.charVel.x * t.charVel.x + t.charVel.z * t.charVel.z);
            bool run = sp > params_.walkSpeed + 0.2f;
            float full = run ? params_.runSpeed : params_.walkSpeed;
            b3Vec3 wish = full > 0 ? mul(b3Vec3{t.charVel.x, 0, t.charVel.z}, 1 / full) : b3Vec3{0, 0, 0};
            float sy = std::sin(t.yaw), cy = std::cos(t.yaw);
            p.guess = {};
            p.guess.yaw = t.yaw;
            p.guess.run = run;
            p.guess.moveX = std::clamp(wish.x * cy - wish.z * sy, -1.0f, 1.0f);    // right = (cos, 0, -sin)
            p.guess.moveZ = std::clamp(-wish.x * sy - wish.z * cy, -1.0f, 1.0f);   // forward = (-sin, 0, -cos)
            if (sp < 0.3f) p.guess.moveX = p.guess.moveZ = 0;
            p.yawRate = t.yawRate;
        }
        if (p.simulated) {   // back to the snapshot state (asleep if the server's body sleeps)
            scene_->setTransform(p.handle, p.pos, p.rot);
            scene_->setLinearVelocity(p.handle, p.vel);
            scene_->setAngularVelocity(p.handle, p.spin);
            scene_->setAwake(p.handle, !t.sleeping);
            p.dirty = false;
        }
    }
}

void PredictionWorld::place() {
    for (auto& [id, p] : proxies_) {
        if (!p.dirty || p.simulated) continue;
        p.dirty = false;
        scene_->setTransform(p.handle, p.pos, p.rot);
        scene_->setLinearVelocity(p.handle, p.vel);      // what a character standing on it is carried by
        scene_->setAngularVelocity(p.handle, p.spin);
    }
    const float dt = scene_->dt();
    for (const auto& k : kinematic_) {
        if (!k.moves || k.handle == kNoBody) continue;
        // The server's body at tick T: moved to moverPosition(T) during the step from T - 1, with that velocity.
        b3Vec3 at = moverPosition(k.start, k.offset, k.period, tick_, dt);
        b3Vec3 before = moverPosition(k.start, k.offset, k.period, tick_ > 0 ? tick_ - 1 : 0, dt);
        scene_->setTransform(k.handle, at, k.rot);
        scene_->setLinearVelocity(k.handle, mul(sub(at, before), 1 / dt));
    }
}

void PredictionWorld::push(const physics::Pushes& pushes) {
    physics::Pushes simulatedPushes;
    for (const auto& u : pushes) {
        auto h = byHandle_.find(u.body);
        if (h == byHandle_.end()) continue;
        Proxy& p = proxies_[h->second];
        if (p.character) continue;
        if (p.simulated) { simulatedPushes.push_back(u); continue; }
        // A kinematic stand-in: applyPushes' response with the replicated mass.
        float rel = u.speed - dot(p.vel, u.direction);
        if (rel <= 0) continue;
        p.vel = add(p.vel, mul(u.direction, std::min(rel, u.maxImpulse / p.mass)));
        p.dirty = true;
    }
    scene_->applyPushes(simulatedPushes);
}

void PredictionWorld::advance() {
    const float dt = scene_->dt();
    ++tick_;
    for (auto& [id, p] : proxies_) {
        if (p.simulated || p.ahead >= kMaxAheadTicks || (p.character && stepOthers)) continue;
        ++p.ahead;
        if (dot(p.vel, p.vel) < 1e-10f && dot(p.spin, p.spin) < 1e-10f) continue;
        p.pos = add(p.pos, mul(p.vel, dt));
        p.rot = integrate(p.rot, p.spin, dt);
        p.dirty = true;
        if (p.ahead == kMaxAheadTicks) { p.vel = {0, 0, 0}; p.spin = {0, 0, 0}; }   // hold from here
    }
}

void PredictionWorld::pushAction(const physics::CharacterState& st) {
    if (!pushTicks_.empty() && int32_t(tick_ - pushTicks_.back()) < int32_t(push_.cooldown)) return;
    pushTicks_.push_back(tick_);
    // PlaceServer::push: a mass-scaled impulse to the dynamic bodies in a sphere in front of the character.
    float s = std::sin(st.yaw), c = std::cos(st.yaw);
    b3Vec3 fwd{-s, 0, -c};
    b3Vec3 centre = add(add(st.position, mul(fwd, push_.reach)), {0, params_.height * 0.5f, 0});
    b3Vec3 dir{fwd.x * 0.94f, 0.34f, fwd.z * 0.94f};
    physics::QueryFilter f;
    f.mask = physics::kCategoryDynamic;
    for (BodyHandle h : scene_->overlapSphere(centre, push_.radius, f)) {
        auto o = byHandle_.find(h);
        if (o == byHandle_.end()) continue;
        Proxy& p = proxies_[o->second];
        float dv = std::min(p.mass, push_.maxMass) * push_.speed / p.mass;
        if (p.simulated) scene_->applyImpulse(h, mul(dir, dv * p.mass));
        else { p.vel = add(p.vel, mul(dir, dv)); p.dirty = true; }
    }
}

void PredictionWorld::step(physics::CharacterState& st, const Input& in) {
    place();
    physics::Pushes pushes;
    physics::stepCharacter(scene_->world(), params_, st, in.move, scene_->dt(), &pushes);
    push(pushes);
    if (stepOthers) {   // the other characters against the same world, as the server steps them in the same tick
        for (auto& [id, p] : proxies_) {
            if (!p.character || p.ahead >= kMaxAheadTicks) continue;
            ++p.ahead;
            p.guess.yaw += p.yawRate;   // keeps turning like the mouse did
            physics::Pushes theirs;
            physics::stepCharacter(scene_->world(), params_, p.state, p.guess, scene_->dt(), &theirs, p.handle);
            push(theirs);
            p.pos = p.state.position;
            p.vel = p.state.velocity;
            p.dirty = true;
        }
    }
    if (in.actions & kActionPush) pushAction(st);
    if (simulated()) scene_->step();   // integrates the simulated bodies (stand-ins are placed by advance / place)
    advance();
}
}
