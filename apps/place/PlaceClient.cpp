#include "apps/place/PlaceClient.h"
#include <cmath>
#include <cstring>

namespace aiverse::place {
namespace {
float yawOf(const replica::Value& q) { return 2 * std::atan2(q.f[1], q.f[3]); }
}

PlaceClient::PlaceClient(std::unique_ptr<net::Conn> connection, replica::ClientOptions options)
    : client_(schema_.schema, std::move(connection), options), params_(characterParams()), smoother_({50.0f, 3.0f}) {
    client_.onMessage = [this](uint16_t channel, const net::Bytes& payload) {
        if (channel == kChannelScene && !scene_) {
            try {
                sceneJson_ = Json::parse(std::string(payload.begin(), payload.end()));
                scene_ = physics::Scene::staticCopy(sceneJson_);
                looks_ = bodyLooks(sceneJson_);
            } catch (const std::exception&) { scene_.reset(); }
        } else if (channel == kChannelPing && payload.size() == 8) {
            uint64_t sent = 0;
            std::memcpy(&sent, payload.data(), 8);
            if (nowMs_ >= sent) stats_.rttMs = uint32_t(nowMs_ - sent);
        } else if (channel == kChannelSpawnDenied) {
            ++stats_.spawnDenied;
        }
    };
}

PlaceClient::~PlaceClient() = default;

bool PlaceClient::ready() const { return scene_ && own_ && predictor_; }

const physics::CharacterState& PlaceClient::predicted() const {
    static const physics::CharacterState none;
    return predictor_ ? predictor_->state() : none;
}

b3Vec3 PlaceClient::drawPosition() const {
    b3Vec3 p = predicted().position;
    float dx, dz, dy, unused;
    smoother_.offset(1, dx, dz);
    smoother_.offset(2, dy, unused);
    return {p.x + dx, p.y + dy, p.z + dz};
}

void PlaceClient::spawnBox() { client_.send(kChannelSpawnBox, {}); }

physics::CharacterState PlaceClient::authoritative() const {
    physics::CharacterState s;
    const auto& p = client_.latest(own_, Schema::kCharPos).f;
    const auto& v = client_.latest(own_, Schema::kCharVelocity).f;
    s.position = {p[0], p[1], p[2]};
    s.velocity = {v[0], v[1], v[2]};
    s.yaw = yawOf(client_.latest(own_, Schema::kCharYaw));
    s.grounded = client_.latest(own_, Schema::kCharGrounded).i != 0;
    return s;
}

void PlaceClient::findOwn() {
    if (own_ && predictor_ && client_.exists(own_)) return;
    own_ = 0;
    predictor_.reset();
    for (auto id : client_.objects())
        if (client_.classOf(id) == schema_.character && client_.ownerOf(id) == client_.slot()) { own_ = id; break; }
    if (!own_ || !scene_) return;
    physics::Scene* world = scene_.get();
    auto params = params_;
    predictor_ = std::make_unique<replica::Predictor<physics::CharacterState, Input>>(
        [world, params](physics::CharacterState& st, const Input& in) { physics::stepCharacter(world->world(), params, st, in.move, world->dt()); });
    predictor_->reset(authoritative());
    reconciledTick_ = client_.latestTick();
    smoother_.clear();
}

void PlaceClient::reconcile() {
    if (!predictor_ || client_.latestTick() == reconciledTick_) return;
    reconciledTick_ = client_.latestTick();
    b3Vec3 before = predictor_->state().position;
    predictor_->reconcile(authoritative(), client_.lastAppliedInput(), [](const auto&, const auto&) { return 0; });
    b3Vec3 after = predictor_->state().position;
    float dx = before.x - after.x, dy = before.y - after.y, dz = before.z - after.z;
    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    ++stats_.reconciles;
    stats_.sumCorrection += d;
    stats_.lastCorrection = d;
    if (d > stats_.maxCorrection) stats_.maxCorrection = d;
    if (d > 0.01f) ++stats_.corrections;
    if (d > 1e-5f) { smoother_.correct(1, dx, dz); smoother_.correct(2, dy, 0); }
}

void PlaceClient::poll(uint64_t nowMs) {
    nowMs_ = nowMs;
    client_.poll(nowMs);
    if (!windowStart_) windowStart_ = nowMs;
    if (nowMs - lastPing_ >= 1000 && client_.connected() && client_.slot()) {
        lastPing_ = nowMs;
        net::Bytes b(8);
        std::memcpy(b.data(), &nowMs, 8);
        client_.send(kChannelPing, b);
    }
    if (nowMs - windowStart_ >= 1000) {
        const auto& s = client_.stats();
        float sec = float(nowMs - windowStart_) / 1000.0f;
        stats_.snapshotsPerSecond = float(s.snapshots - windowSnapshots_) / sec;
        stats_.bytesInPerSecond = float(s.bytesIn - windowIn_) / sec;
        stats_.bytesOutPerSecond = float(s.bytesOut - windowOut_) / sec;
        windowSnapshots_ = s.snapshots; windowIn_ = s.bytesIn; windowOut_ = s.bytesOut;
        windowStart_ = nowMs;
    }
    if (!client_.ready()) return;
    findOwn();
    reconcile();
}

bool PlaceClient::update(uint64_t nowMs, const Input& input) {
    poll(nowMs);
    if (!ready()) return false;
    // Predict exactly what the server will see: the quantized frame.
    net::Bytes frame = input.pack();
    uint32_t seq = client_.sendInput(frame);
    predictor_->local(seq, Input::unpack(frame));
    return true;
}
}
