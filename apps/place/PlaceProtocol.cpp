#include "apps/place/PlaceProtocol.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::place {
using replica::ClassDef;
using replica::FieldKind;

Schema::Schema() {
    // Bodies: position / rotation interpolated, look on the reliable path, sleeping as a cheap flag.
    ClassDef b{"body", {}, 1.0f, 0, false, 0};
    b.fields = {{"pos", FieldKind::Vec3, -1024, 1024, 0.01, 0, false, true},
                {"rot", FieldKind::Quat, 0, 0, 0, 11, false, true},
                {"shape", FieldKind::Int, 0, 2, 0, 0, true},
                {"size", FieldKind::Vec3, 0, 100, 0.01, 0, true},
                {"color", FieldKind::Int, 0, 0xFFFFFF, 0, 0, true},
                {"sleeping", FieldKind::Bool},
                {"kinematic", FieldKind::Int, 0, 65535, 0, 0, true},
                {"velocity", FieldKind::Vec3, -32, 32, 0.02},
                {"density", FieldKind::Float, 0, 100000, 0.5, 0, true},
                {"friction", FieldKind::Float, 0, 2, 0.01, 0, true}};
    body = schema.add(b);
    // Characters: the owner reconciles against pos / velocity / grounded, so velocity is fine-grained.
    ClassDef c{"character", {}, 4.0f, 0, false, 0};
    c.fields = {{"pos", FieldKind::Vec3, -1024, 1024, 0.005, 0, false, true},
                {"yaw", FieldKind::Quat, 0, 0, 0, 10, false, true},
                {"velocity", FieldKind::Vec3, -64, 64, 0.005},
                {"grounded", FieldKind::Bool},
                {"name", FieldKind::String},
                {"color", FieldKind::Int, 0, 0xFFFFFF, 0, 0, true},
                {"input", FieldKind::Int, 0, 65535}};
    character = schema.add(c);
    ClassDef g{"game", {}, 2.0f, 0, true, -1};
    g.fields = {{"tick", FieldKind::Int, 0, 0}, {"players", FieldKind::Int, 0, 255}, {"bodies", FieldKind::Int, 0, 65535}};
    game = schema.add(g);
    if (schema.field(body, "friction") != kBodyFriction || schema.field(character, "input") != kCharInput ||
        schema.field(game, "bodies") != kGameBodies)
        throw std::logic_error("place schema field order");
}

net::Bytes Input::pack() const {
    auto b = move.pack();
    b.push_back(actions);
    return b;
}

Input Input::unpack(const net::Bytes& bytes) {
    Input in;
    in.move = physics::CharacterInput::unpack(bytes.data(), std::min(bytes.size(), physics::CharacterInput::kPackedSize));
    in.actions = bytes.size() > physics::CharacterInput::kPackedSize ? uint8_t(bytes[physics::CharacterInput::kPackedSize] & kActionPush) : 0;
    return in;
}

uint32_t colorOf(const Json& rgb, uint32_t fallback) {
    if (!rgb.isArray() || rgb.size() != 3) return fallback;
    uint32_t out = 0;
    for (size_t i = 0; i < 3; ++i) {
        double v = rgb[i].isNumber() ? rgb[i].toDouble() : 0;
        out = (out << 8) | uint32_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0) * 255));
    }
    return out;
}

std::vector<BodyLook> bodyLooks(const Json& scene) {
    std::vector<BodyLook> out;
    const auto& bodies = scene["bodies"];
    for (size_t i = 0; i < bodies.size(); ++i) {
        const auto& j = bodies[i];
        BodyLook l;
        l.id = j.getStr("id");
        auto motion = j.getStr("motion", "dynamic");
        l.motion = motion == "static" ? physics::Motion::Static : motion == "kinematic" ? physics::Motion::Kinematic : physics::Motion::Dynamic;
        auto shape = j.getStr("shape", "box");
        auto num = [&](const char* key, double def) { return float(j[key].isNumber() ? j[key].toDouble() : def); };
        if (shape == "sphere") {
            l.shape = Shape::Sphere;
            float r = num("radius", 0.5);
            l.size = {r, r, r};
        } else if (shape == "capsule") {
            l.shape = Shape::Capsule;
            l.size = {num("radius", 0.4), num("halfHeight", 0.6), num("radius", 0.4)};
        } else {
            l.shape = Shape::Box;
            l.drawable = shape == "box";
            const auto& s = j["size"];
            if (s.isArray() && s.size() == 3)
                for (size_t k = 0; k < 3; ++k) l.size[k] = float(s[k].toDouble());
        }
        uint32_t fallback = l.motion == physics::Motion::Static ? 0x5A6672u : 0xC9A25Au;
        l.color = colorOf(j["color"], fallback);
        out.push_back(std::move(l));
    }
    return out;
}

Json staticPart(const Json& scene) {
    Json out = Json::object();
    for (const auto& [key, value] : scene.members()) {
        if (key != "bodies" && key != "joints") { out.set(key, value); continue; }
        if (key == "joints") continue;
        Json bodies = Json::array();
        for (const auto& b : value.items())
            if (b.getStr("motion", "dynamic") != "dynamic") bodies.push(b);
        out.set("bodies", std::move(bodies));
    }
    return out;
}

b3Vec3 moverPosition(b3Vec3 start, b3Vec3 offset, float period, uint64_t tick, float dt) {
    double t = double(tick) * dt;
    float k = float(0.5 - 0.5 * std::cos(2 * 3.14159265358979 * t / period));
    return {start.x + offset.x * k, start.y + offset.y * k, start.z + offset.z * k};
}

physics::CharacterParams characterParams() {
    physics::CharacterParams p;
    p.walkSpeed = 4.5f;
    p.runSpeed = 8.0f;
    p.jumpSpeed = 7.0f;
    p.pushImpulseLimit = 40.0f;
    return p;
}
}
