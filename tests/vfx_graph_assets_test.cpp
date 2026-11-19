// Versioned graph asset registry, parameter variants and safe hot reload.
#include "engine/fx/SpectraFX.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
static void check(bool ok,const char* reason) {
    if(!ok) throw std::runtime_error(reason);
}
int main() {
    std::string file = R"JSON({
      "version":1,
      "parameters":{"size":0.25},
      "nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":4}},
        {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"size":{"$param":"size"}}},
        {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:smoke"}}
      ],
      "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
      "outputs":["out"]
    })JSON";
    spectra::Runtime ps;
    ps.setQuality(fx::FxQuality::High);
    check(ps.init(nullptr),"init CPU backend");
    int readCount=0;
    spectra::GraphAssets assets(ps,[&](const std::string& path,std::string& contents) {
        ++readCount;
        if(path!="fx/graphs/smoke.vfx.json")return false;
        contents=file;
        return true;
    });
    auto a=assets.load("smoke");
    check(a.effect>=0 && a.recompiled && !a.fallback,"initial graph loading");
    check(ps.def(a.effect)->emitters[0].burst==4,"wrong compiled spawn");
    auto cached=assets.load("smoke");
    check(cached.effect==a.effect && !cached.recompiled && readCount==2,"unchanged content must reuse compiled effect");
    const Json override=Json::parse(R"JSON({"size":1.25})JSON");
    auto b=assets.load("smoke",override);
    check(b.effect>=0 && b.effect!=a.effect && b.recompiled,"parameter variant is not independent");
    check(ps.def(b.effect)->emitters[0].size.min==1.25f,"parameter override");
    check(assets.variants()==2,"two cached parameter variants");

    file=R"JSON({
      "version":1,"parameters":{"size":0.25},
      "nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":9}},
        {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"size":{"$param":"size"}}},
        {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:smoke"}}
      ],
      "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
      "outputs":["out"]
    })JSON";
    auto newer=assets.load("smoke");
    check(newer.effect==a.effect && newer.recompiled,"changed content must hot reload in-place");
    check(ps.def(newer.effect)->emitters[0].burst==9,"updated graph not compiled");
    file="{ broken JSON";
    auto fallback=assets.load("smoke");
    check(fallback.fallback && fallback.effect==a.effect && !fallback.errors.empty(),"invalid graph should preserve last good effect");
    check(ps.def(a.effect)->emitters[0].burst==9,"bad hot reload destroyed old effect");
    check(assets.load("../other").effect<0,"unsafe traversal name accepted");
    assets.invalidate("smoke");
    check(assets.variants()==0,"invalidate should clear named variants");
    std::cout<<"PASS vfx_graph_assets_test (load, cache, override, hot reload, fallback, name validation)\n";
}
