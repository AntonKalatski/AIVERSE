#include "apps/studio-native/StudioFpsLightingDocument.h"
#include <cassert>
#include <iostream>

using namespace aiverse;
using namespace aiverse::studio;
int main() {
    Json doc=Json::parse(R"({"name":"FPS Arena","colliders":[],"spawns":[[1,2,3]]})");
    assert(fpsLightingView(doc)["lights"].size()==0);
    doc=editFpsLighting(doc,"studio.lightAdd",Json::object());
    assert(doc["lights"].size()==1);
    assert(doc["spawns"][0][0].toDouble()==1);
    Json p=Json::parse(R"({"index":0,"key":"intensity","value":3.5})");
    doc=editFpsLighting(doc,"studio.lightUpdate",p);
    assert(doc["lights"][0]["intensity"].toDouble()==3.5);
    Json scalar=Json::parse(R"({"key":"storm","value":0.45})");
    doc=editFpsLighting(doc,"studio.lightingSet",scalar);
    assert(doc["lighting"]["storm"].toDouble()==0.45);
    assert(fpsLightingView(doc)["lighting"]["storm"].toDouble()==0.45);
    const Json before=doc;
    bool rejected=false;
    try { (void)editFpsLighting(doc,"studio.lightingSet",Json::parse(R"({"key":"storm","value":2})")); }
    catch (const std::exception&) { rejected=true; }
    assert(rejected && doc == before);
    rejected=false;
    try { (void)editFpsLighting(doc,"studio.lightDelete",Json::parse(R"({"index":0.5})")); }
    catch (const std::exception&) { rejected=true; }
    assert(rejected && doc == before);
    doc=editFpsLighting(doc,"studio.lightDelete",Json::parse(R"({"index":0})"));
    assert(doc["lights"].size()==0);
    std::cout << "Studio FPS Lighting serialization PASS\n";
}
