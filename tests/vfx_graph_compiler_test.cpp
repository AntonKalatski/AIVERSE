// Niagara-style graph compiler v1: standalone, genre-independent CPU smoke.
#include "engine/fx/VfxGraph.h"
#include "engine/fx/ParticleSystem.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
static void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

int main() {
    const Json base = Json::parse(R"JSON({
      "version": 1,
      "parameters": {
        "size": 0.5,
        "fade": [[0, "#ffffffff"], [1, "#ffffff00"]]
      },
      "nodes": [
        {"id":"spawn", "type":"Spawn", "properties":{"burst":6,"size":{"$param":"size"}}},
        {"id":"initialize","type":"Initialize","properties":{"life":1,"speed":0}},
        {"id":"update","type":"Update","properties":{"color":{"$param":"fade"}}},
        {"id":"sprite","type":"SpriteOutput","properties":{"texture":"builtin:spark"}},
        {"id":"ribbon","type":"RibbonOutput","properties":{"trail":{"time":0.6,"width":[[0,1],[1,0]]}}}
      ],
      "links": [
        {"from":"spawn","to":"initialize"},
        {"from":"initialize","to":"update"},
        {"from":"update","to":"sprite"},
        {"from":"update","to":"ribbon"}
      ],
      "outputs": ["sprite", "ribbon"]
    })JSON");
    fx::EffectDef effect;
    std::vector<std::string> errors;
    check(fx::compileVfxGraph(base,effect,errors), "valid graph compile");
    check(effect.emitters.size()==2, "two output branches must generate two emitters");
    check(effect.emitters[0].burst==6, "spawn overlay lost");
    check(effect.emitters[0].size.min==0.5f, "default parameter lost");
    check(effect.emitters[1].hidden && effect.emitters[1].trail.enabled,
          "RibbonOutput must enable hidden ribbon particles");
    fx::ParticleSystem ps;
    ps.setQuality(fx::FxQuality::High);
    check(ps.init(nullptr), "headless particle init");
    const int id=ps.define("graph_smoke",effect);
    check(id>=0,"compiled effect must be accepted by legacy backend");
    check(ps.play(id,{0,0,0}).valid(), "compiled graph should play");
    ps.update(0.1f);
    std::vector<fx::FxParticleInfo> particles;
    ps.snapshot(particles);
    check(!particles.empty(), "compiled effect should emit particles");

    const Json override = Json::parse(R"JSON({"size":2})JSON");
    check(fx::compileVfxGraph(base,override,effect,errors), "override compile");
    check(effect.emitters[0].size.min==2.0f, "parameter override lost");

    Json invalid = base;
    invalid.set("version",Json::number(int64_t(2)));
    check(!fx::compileVfxGraph(invalid,effect,errors), "invalid version accepted");
    invalid = base;
    invalid.set("outputs",Json::parse(R"JSON(["update"])JSON"));
    check(!fx::compileVfxGraph(invalid,effect,errors), "non-output accepted");
    invalid = base;
    invalid.set("links",Json::parse(R"JSON([
        {"from":"spawn","to":"initialize"},
        {"from":"initialize","to":"update"},
        {"from":"update","to":"sprite"},
        {"from":"update","to":"ribbon"},
        {"from":"sprite","to":"spawn"}])JSON"));
    check(!fx::compileVfxGraph(invalid,effect,errors), "invalid graph cycle accepted");
    invalid = base;
    invalid.set("parameters",Json::object());
    check(!fx::compileVfxGraph(invalid,effect,errors), "unbound parameter accepted");
    invalid = base;
    invalid.set("outputs",Json::parse(R"JSON(["sprite","sprite"])JSON"));
    check(!fx::compileVfxGraph(invalid,effect,errors), "duplicate outputs accepted");

    std::cout<<"PASS vfx_graph_compiler_test (graph branching, CPU backend, parameters, validation)\n";
}
