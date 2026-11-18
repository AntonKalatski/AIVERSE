// PRISMA camera volumes: JSON merge, partial profiles, masks and deterministic blending.
#include "engine/render/PrismaVolumeData.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace aiverse;
using namespace aiverse::render;

static int errors = 0;
static void expect(bool ok, const char* label) {
    if (!ok) { ++errors; std::fprintf(stderr, "FAIL: %s\n", label); }
}
static float exposure(const PrismaSettings& s) {
    const auto* e = s.find(PrismaType::ColorGrade);
    return e ? e->get("exposure") : 0.f;
}
int main() {
    const Json game = Json::parse(R"({
      "volumes":[
        {"id":"warm","shape":"sphere","center":[0,0,0],"radius":2,
          "blendDistance":2,"profile":{"effects":[{"type":"colorGrade","exposure":2}]}},
        {"id":"global","shape":"global","priority":-1,"weight":0,
          "profile":{"effects":[{"type":"vignette","intensity":0.3}]}}
      ]
    })");
    std::vector<std::string> issues;
    auto stack = parsePrismaVolumes(game, Json(), &issues);
    expect(issues.empty(),"valid game volume JSON");
    expect(stack.volumes.size()==2,"two profiles loaded");
    auto base = parsePrisma(Json::parse(R"({"hdr":"on","effects":[{"type":"colorGrade","exposure":0}]})"),Json());
    expect(stack.resolve(base,{0,0,0}).hdr==HdrMode::On,"partial profile does not reset HDR");
    expect(std::abs(exposure(stack.resolve(base,{3,0,0}))-1.f)<0.001f,"sphere blend distance");
    expect(std::abs(exposure(stack.resolve(base,{8,0,0})))<0.001f,"outside volume retains original");
    expect(std::abs(exposure(stack.resolve(base,{0,0,0}))-2.f)<0.001f,"inside volume has full exposure");

    const Json map = Json::parse(R"({
      "volumes":[
        {"id":"warm","shape":"box","center":[0,0,0],"halfExtents":[1,1,1],
         "priority":10,"layerMask":2,
         "profile":{"effects":[{"type":"colorGrade","exposure":4}]}}
      ]
    })");
    auto override = parsePrismaVolumes(game,map,&issues);
    expect(issues.empty(),"map override valid");
    expect(override.volumes.size()==2,"map matches existing volume ID, no duplicates");
    expect(std::abs(exposure(override.resolve(base,{0,0,0},2))-4.f)<0.001f,"map volume wins");
    expect(std::abs(exposure(override.resolve(base,{0,0,0},1)))<0.001f,"layer mask excludes camera");

    // Full 32-bit mask must bypass Json::integer's fixed-point numeric path.
    const Json allMask = Json::parse(R"({"volumes":[
      {"id":"full","shape":"global","layerMask":4294967295,
       "profile":{"effects":[{"type":"colorGrade","exposure":1}]}}]})");
    issues.clear();
    const auto parsedMask = parsePrismaVolumes(allMask,Json(),&issues);
    expect(issues.empty() && parsedMask.volumes.size()==1 &&
           parsedMask.volumes[0].layerMask==0xffffffffu,
           "all camera bits survive serialization");

    const Json reset = Json::parse(R"({"replace":true,"volumes":[]})");
    expect(parsePrismaVolumes(game,reset).volumes.empty(),"map replace clears game volume list");

    PrismaVolume volume;
    volume.shape=PrismaVolumeShape::Global;
    volume.profile=parsePrisma(Json::parse(R"({"hdr":"off","effects":[{"type":"colorGrade","exposure":3}]})"),Json());
    PrismaVolumeStack manual;
    manual.volumes.push_back(volume);
    expect(manual.resolve(base,{0,0,0}).hdr==HdrMode::On,"partial native profile HDR not overwritten");
    manual.volumes[0].overrideHdr=true;
    expect(manual.resolve(base,{0,0,0}).hdr==HdrMode::Off,"explicit HDR override");
    manual.volumes[0].weight=0.25f;
    expect(std::abs(exposure(manual.resolve(base,{0,0,0}))-0.75f)<0.001f,"color grading fades from neutral");

    const Json bad = Json::parse(R"({"volumes":[{"shape":"sphere","center":[0,0,0],"radius":-4,
                          "profile":{"effects":[]}}]})");
    issues.clear();
    auto rejected = parsePrismaVolumes(bad,Json(),&issues);
    expect(rejected.volumes.empty()&&!issues.empty(),"invalid radius rejected with diagnostics");

    const Json duplicates = Json::parse(R"({"volumes":[
      {"id":"x","profile":{"effects":[]}},
      {"id":"x","profile":{"effects":[]}}]})");
    issues.clear();
    auto dedup = parsePrismaVolumes(duplicates,Json(),&issues);
    expect(dedup.volumes.size()==1 && !issues.empty(),"duplicate id rejected");

    std::printf("PRISMA volume profiles: %d failures\n",errors);
    return errors ? 1 : 0;
}
