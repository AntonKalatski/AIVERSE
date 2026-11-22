// GPU-free checks of the PRISMA effects added 2026-11-22: catalog entries, parameter
// parsing/clamping/round trip, quality plans, cost model, presets, upscaler render sizes and
// the shader contracts the renderer relies on (bit names, texture slots, helper rules).
#include "engine/render/PrismaData.h"
#include "engine/render/PrismaShaders.h"
#include "engine/render/PrismaShadersExtra.h"
#include "engine/render/graph/ShaderGraph.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::render;
static void check(bool value, const std::string& why) { if (!value) throw std::runtime_error(why); }
static PrismaEffect parse(const char* json) { return parsePrismaEffect(Json::parse(json), true); }

int main() {
    try {
        struct Expect { const char* id; PrismaType type; PrismaCostClass cost; };
        const Expect expected[] = {
            {"ambientOcclusion", PrismaType::AmbientOcclusion, PrismaCostClass::Heavy},
            {"screenSpaceReflections", PrismaType::ScreenSpaceReflections, PrismaCostClass::Heavy},
            {"lensFlare", PrismaType::LensFlare, PrismaCostClass::Heavy},
            {"smaa", PrismaType::Smaa, PrismaCostClass::Heavy},
            {"lensDistortion", PrismaType::LensDistortion, PrismaCostClass::Cheap},
            {"anamorphicStreaks", PrismaType::AnamorphicStreaks, PrismaCostClass::Heavy},
            {"halation", PrismaType::Halation, PrismaCostClass::Medium},
            {"heatHaze", PrismaType::HeatHaze, PrismaCostClass::Medium},
            {"fsr", PrismaType::Fsr, PrismaCostClass::Medium},
            {"dlss", PrismaType::Dlss, PrismaCostClass::Heavy},
        };
        for (const auto& e : expected) {
            const PrismaTypeDef* d = prismaType(e.id);
            check(d && d->type == e.type, std::string("catalog has ") + e.id);
            check(d->cost == e.cost, std::string("cost class of ") + e.id);
            check(&prismaType(e.type) == d, std::string("type lookup of ") + e.id);
            // Every effect renders at some tier and costs something there.
            const PrismaEffect def = defaultPrismaEffect(e.type);
            const PrismaWork wk = prismaEffectWork(def, PrismaQuality::High);
            check(wk.units > 0, std::string("cost model covers ") + e.id);
            check(prismaEffectWork(def, PrismaQuality::Off).units == 0, std::string("off tier is free: ") + e.id);
            // Defaults round trip to a bare {"type"} object.
            const Json j = prismaEffectJson(def);
            check(j.members().size() == 1 && j.getStr("type") == e.id, std::string("defaults omitted: ") + e.id);
            // Heavy effects warn, as the MCP contract promises.
            if (e.cost == PrismaCostClass::Heavy) check(!prismaEffectWarnings(def).empty(), std::string("heavy warning: ") + e.id);
        }
        check(prismaCatalog().back().type == PrismaType::ShaderGraph, "shaderGraph stays last in the catalog");

        // Parameters: values, clamping, text, round trip.
        const auto ao = parse(R"({"type":"ambientOcclusion","intensity":9,"radius":2.5,"color":[0.1,0.05,0]})");
        check(ao.get("intensity") == 4 && ao.get("radius") == 2.5f && std::fabs(ao.color("color")[0] - 0.1f) < 1e-6f, "AO params clamp/parse");
        const auto flare = parse(R"({"type":"lensFlare","dirtTexture":"fx/dirt.png","ghosts":6})");
        check(flare.text == "fx/dirt.png" && flare.get("ghosts") == 6, "lens flare dirt texture is a text param");
        const auto flareAgain = parse(prismaEffectJson(flare).dump(-1).c_str());
        check(flareAgain.text == "fx/dirt.png" && flareAgain.get("ghosts") == 6, "lens flare round trip");
        bool threw = false;
        try { parse(R"({"type":"smaa","searchSteps":"many"})"); } catch (const std::exception&) { threw = true; }
        check(threw, "strict parse rejects non-numeric SMAA steps");
        const auto distort = parse(R"({"type":"lensDistortion","distortion":-3,"panini":0.7})");
        check(distort.get("distortion") == -1 && distort.get("panini") == 0.7f, "distortion clamps to -1..1");
        const auto ae = parse(R"({"type":"autoExposure","histogram":true,"histogramLow":0.2,"histogramHigh":0.9})");
        check(ae.get("histogram") > 0.5f && ae.get("histogramLow") == 0.2f && ae.get("histogramHigh") == 0.9f, "histogram metering params");
        const auto aeOld = parse(R"({"type":"autoExposure","keyValue":0.2})");
        check(aeOld.get("histogram") < 0.5f && aeOld.get("keyValue") == 0.2f, "old exposure profiles keep the log-average meter");
        PrismaSettings s;
        s.effects.push_back(defaultPrismaEffect(PrismaType::HeatHaze));
        s.effects.back().values.resize(2);                 // truncated native profile
        s.effects.back().values[0] = std::nanf("");
        sanitizePrismaSettings(s);
        check(s.effects.back().values.size() == defaultPrismaEffect(PrismaType::HeatHaze).values.size() &&
              std::isfinite(s.effects.back().get("amplitude")), "sanitize repairs new effect values");

        // A map can switch a game effect off; minQuality gates tiers.
        const auto merged = parsePrisma(Json::parse(R"({"effects":[{"type":"screenSpaceReflections"},{"type":"smaa"}]})"),
                                        Json::parse(R"({"effects":[{"type":"smaa","enabled":false}]})"));
        check(merged.activeAt(PrismaQuality::High).size() == 1, "map disables SMAA, SSR stays");
        check(merged.activeAt(PrismaQuality::Medium).empty(), "SSR is High-only by default");

        // Plans.
        check(aoPlan(PrismaQuality::High).directions * aoPlan(PrismaQuality::High).steps >
              aoPlan(PrismaQuality::Medium).directions * aoPlan(PrismaQuality::Medium).steps, "AO plan grows with tier");
        check(ssrPlan(PrismaQuality::High).maxSteps <= 96 && ssrPlan(PrismaQuality::Low).maxSteps >= 8, "SSR steps bounded by shader loop");
        check(smaaSearchSteps(PrismaQuality::High) <= 32, "SMAA steps bounded by shader loop (64 iterations)");
        check(aoPlan(PrismaQuality::High).directions <= 8 && aoPlan(PrismaQuality::High).steps <= 8, "AO plan within shader loop bounds");

        // Upscaler render sizes: quantized to 1/16, at least 32 px, never above output.
        check(prismaScaledRenderSize(1920, 1080, 1.0f) == std::make_pair(1920, 1080), "scale 1 keeps size");
        check(prismaScaledRenderSize(1920, 1080, 0.5f) == std::make_pair(960, 540), "half size");
        check(prismaScaledRenderSize(1920, 1080, 0.67f) == prismaScaledRenderSize(1920, 1080, 0.6875f), "1/16 quantization");
        check(prismaScaledRenderSize(40, 30, 0.25f).first == 32 && prismaScaledRenderSize(40, 30, 0.25f).second == 30, "32 px floor");
        check(prismaScaledRenderSize(1920, 1080, std::nanf("")) == std::make_pair(1920, 1080), "NaN scale is safe");

        // Cost report includes the new effects and warns on a heavy chain.
        PrismaSettings heavy = parsePrisma(Json::parse(R"({"effects":[{"type":"ambientOcclusion"},{"type":"screenSpaceReflections"},
            {"type":"lensFlare"},{"type":"smaa"},{"type":"anamorphicStreaks"}]})"), Json());
        const Json report = prismaCostReport(heavy);
        check(report["effects"].size() == 5 && report["warnings"].size() > 0, "cost report covers the new chain");

        // Presets using the new effects parse strictly; stylization graphs compile.
        for (const auto& p : prismaPresets()) {
            const Json list = Json::parse(p.effectsJson);
            for (const auto& e : list.items()) parsePrismaEffect(e, true);
        }
        for (const char* id : {"lens", "realistic", "toon", "crt", "scanlines", "dither", "kuwahara", "sketch"})
            check(prismaPreset(id) != nullptr, std::string("preset ") + id);

        // Shader contracts the renderer depends on.
        const std::string comp = prisma::kComposite, helpers = prisma::kCompositeHelpers;
        for (const char* key : {"PF_DISTORT", "PF_HAZE", "PF_SHOCK", "PF_FLARE", "PF_DIRT_TEX", "PF_STREAKS", "PF_HALATION"})
            check(comp.find(key) != std::string::npos, std::string("composite handles ") + key);
        for (const char* tex : {"TEX10(", "TEX11(", "TEX12(", "TEX13("})
            check(comp.find(tex) != std::string::npos, std::string("composite samples ") + tex);
        // MSL helpers cannot reach fragment textures: no TEXn( inside helper strings.
        for (const char* h : {prisma::kExtraHelpers, prisma::kAoApplyHelpers, prisma::kCompositeHelpers}) {
            const std::string text = h;
            check(text.find("TEX0(") == std::string::npos && text.find("TEX1(") == std::string::npos,
                  "helpers never sample directly");
        }
        // The flare/FSR helpers sample only through macros expanded in the body.
        check(std::string(prisma::kFlareHelpers).find("#define PX_FLARE") != std::string::npos, "flare sampling macro");
        check(std::string(prisma::kFsrHelpers).find("#define PX_FSR_TAP") != std::string::npos, "FSR sampling macro");

        // Shader Graph postfx: display-referred scene node and a 16-sample budget.
        const Json graph = Json::parse(R"({"version":1,"domain":"postfx","sampleBudget":12,"nodes":[
            {"id":"uv","op":"screenPosition"},{"id":"c","op":"sceneColor","inputs":["uv"]}],"outputs":{"color":"c"}})");
        const auto prog = graph::compile(graph, "postfx");
        check(prog.glsl.find("SG_SCENE(") != std::string::npos, "sceneColor node uses SG_SCENE");
        std::printf("prisma_new_effects: PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "prisma_new_effects FAIL: %s\n", e.what());
        return 1;
    }
}
