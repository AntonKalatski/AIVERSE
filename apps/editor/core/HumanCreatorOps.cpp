#include "HumanCreatorOps.h"
#include "engine/character/vivid/HumanCreatorWorkbench.h"
#include <filesystem>
#include <stdexcept>

namespace aiverse::editor {

namespace {

std::string projectDir(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("no game is open: call open_game first");
    return ws.project.dir;
}

vivid::HumanCreatorWorkbench loadHuman(const std::string& dir, const std::string& name) {
    vivid::HumanCreatorWorkbench human;
    std::string error;
    if (!human.loadDocument(vivid::readHumanCreator(dir, name), &error))
        throw std::runtime_error("humans/" + name + ".json: " + error);
    return human;
}

Json describe(const vivid::HumanCreatorWorkbench& human, const std::string& name, bool withCatalog) {
    Json r = Json::object();
    r.set("ok", Json::fromBool(true));
    r.set("name", Json::string(name));
    r.set("file", Json::string("humans/" + name + ".json"));
    r.set("document", human.toDocument());
    if (withCatalog) r.set("catalog", human.catalog());
    return r;
}

// Applies one human_edit request to a COPY; throws on the first invalid edit.
void applyEdits(vivid::HumanCreatorWorkbench& human, const Json& p, int& changes) {
    std::string error;
    auto fail = [&](const std::string& what) { throw std::invalid_argument(what + (error.empty() ? "" : ": " + error)); };
    if (p.has("preset")) {
        if (!human.preset(p.getStr("preset"), &error)) fail("preset");
        ++changes;
    }
    if (p.has("set")) {
        if (!p["set"].isObject()) fail("'set' must be an object of {key: value}");
        for (const auto& [key, value] : p["set"].members()) {
            if (!human.set(key, value, &error)) fail("set " + key);
            ++changes;
        }
    }
    if (p.has("routes")) {   // replaces the whole route list
        const auto& routes = p["routes"];
        if (!routes.isArray() || routes.size() > 256) fail("'routes' must be an array (max 256)");
        while (!human.routes().empty()) human.removeRoute(human.routes().size() - 1);
        for (size_t i = 0; i < routes.size(); ++i) {
            const auto& r = routes[i];
            const double gain = r["gain"].isNumber() ? r["gain"].toDouble() : 1.0;
            if (!human.setRoute(i, r.getStr("channel"), r.getStr("target"), gain, &error)) fail("route " + std::to_string(i));
        }
        ++changes;
    }
    if (p.has("rig")) {
        if (!p["rig"].isArray()) fail("'rig' must be an array of {node, translation:[x,y,z]}");
        for (const auto& e : p["rig"].items()) {
            const auto& t = e["translation"];
            if (!t.isArray() || t.size() != 3 || !t[0].isNumber() || !t[1].isNumber() || !t[2].isNumber())
                fail("rig translation must be [x,y,z]");
            if (!human.setRig(e.getStr("node"), {float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble())}, &error))
                fail("rig " + e.getStr("node"));
            ++changes;
        }
    }
    if (p.has("skin")) {
        if (!p["skin"].isArray()) fail("'skin' must be an array of {vertex, joint, weight} or {vertex, erase:true}");
        for (const auto& e : p["skin"].items()) {
            const auto& v = e["vertex"];
            if (!v.isNumber() || v.toDouble() < 0 || v.toDouble() > 4294967295.0 || v.toDouble() != double(v.integer()))
                fail("skin vertex must be a non-negative integer");
            const auto vertex = std::uint32_t(v.integer());
            if (e.getBool("erase")) {
                if (!human.eraseSkin(vertex)) fail("no skin edit on vertex " + std::to_string(vertex));
            } else {
                const double w = e["weight"].isNumber() ? e["weight"].toDouble() : -1;
                if (!human.setSkin(vertex, e.getStr("joint"), w, &error)) fail("skin vertex " + std::to_string(vertex));
            }
            ++changes;
        }
    }
}

} // namespace

void registerHumanCreatorOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"human_schema",
        "VIVID digital-human authoring schema (the Studio 'Human Creator' window): sections skin, eyes, groom (hair), anatomy "
        "(face and body proportions), expression (52 ARKit-order rest channels), routes (channel -> morph target, gain 0..4), "
        "rig (bone names), rigCorrections and skinWeights. Each field has key (DNA path for human_edit 'set'), kind, min/max. "
        "Also presets, channel names and the reference human's rig nodes / morph targets / vertex count.",
        Json::parse("{}"), [](const Json&) {
            vivid::HumanCreatorWorkbench human;
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("schema", vivid::HumanCreatorWorkbench::schema());
            r.set("presets", Json::parse(R"(["neutral","warm","cool","stylized"])"));
            r.set("catalog", human.catalog());
            return r;
        }});
    ws.registerOp({"human_list", "Saved digital humans of the open game (humans/<name>.json).", Json::parse("{}"),
        [w](const Json&) {
            Json r = vivid::listHumanCreators(projectDir(*w));
            r.set("ok", Json::fromBool(true));
            return r;
        }});
    ws.registerOp({"human_get", "One saved digital human: its full document (dna, rigCorrections, skinWeights, routes) and the "
        "rig node / morph target names of the generated reference human.",
        Json::parse(R"({"properties": {"name": {"type": "string"}}, "required": ["name"]})"),
        [w](const Json& p) {
            const auto name = p.getStr("name");
            return describe(loadHuman(projectDir(*w), name), name, true);
        }});
    ws.registerOp({"human_create", "Create humans/<name>.json from a preset (neutral | warm | cool | stylized; default neutral). "
        "Fails if it exists unless overwrite:true. Edit it with human_edit; open it in Studio > Human Creator > Load to see it.",
        Json::parse(R"({"properties": {"name": {"type": "string", "description": "A-Z a-z 0-9 _ -, up to 64"},
            "preset": {"type": "string", "enum": ["neutral", "warm", "cool", "stylized"]}, "overwrite": {"type": "boolean"}},
            "required": ["name"]})"),
        [w](const Json& p) {
            const auto dir = projectDir(*w);
            const auto name = p.getStr("name");
            if (!vivid::validHumanCreatorName(name)) throw std::invalid_argument("invalid human name (A-Z a-z 0-9 _ -, up to 64)");
            if (!p.getBool("overwrite") && std::filesystem::exists(vivid::humanCreatorFolder(dir) / (name + ".json")))
                throw std::runtime_error("humans/" + name + ".json exists (pass overwrite:true)");
            vivid::HumanCreatorWorkbench human;
            std::string error;
            if (p.has("preset") && !human.preset(p.getStr("preset"), &error)) throw std::invalid_argument(error);
            vivid::saveHumanCreator(dir, name, human.toDocument());
            Json r = describe(human, name, false);
            r.set("summary", Json::string("Created human " + name));
            return r;
        }});
    ws.registerOp({"human_edit",
        "Edit a saved digital human in one all-or-nothing transaction (nothing is written if any edit is invalid), applied in "
        "this order: preset (resets the DNA), set {key: value} with keys from human_schema (e.g. 'skin.melanin': 0.6, "
        "'eyes.irisColor': [0.2,0.4,0.3], 'groom.lengthMeters': 0.3, 'anatomy.jawWidth': 1.1, 'expressionRest.24': 0.4, "
        "'rig.headBone': 'head'), routes (REPLACES the list: [{channel, target, gain}]), rig ([{node, translation:[x,y,z]}], "
        "|x|,|y|,|z| <= 1, [0,0,0] removes), skin ([{vertex, joint, weight}] or [{vertex, erase:true}]).",
        Json::parse(R"J({"properties": {"name": {"type": "string"},
            "preset": {"type": "string", "enum": ["neutral", "warm", "cool", "stylized"]},
            "set": {"type": "object", "description": "DNA path -> value (number, [r,g,b] colour, string bone, string[] optionalBones)"},
            "routes": {"type": "array", "items": {"type": "object", "properties": {"channel": {"type": "string"}, "target": {"type": "string"}, "gain": {"type": "number"}}}},
            "rig": {"type": "array", "items": {"type": "object", "properties": {"node": {"type": "string"}, "translation": {"type": "array", "items": {"type": "number"}}}}},
            "skin": {"type": "array", "items": {"type": "object", "properties": {"vertex": {"type": "integer"}, "joint": {"type": "string"}, "weight": {"type": "number"}, "erase": {"type": "boolean"}}}}},
            "required": ["name"]})J"),
        [w](const Json& p) {
            const auto dir = projectDir(*w);
            const auto name = p.getStr("name");
            auto human = loadHuman(dir, name);
            int changes = 0;
            applyEdits(human, p, changes);
            if (changes > 0) vivid::saveHumanCreator(dir, name, human.toDocument());
            Json r = describe(human, name, false);
            r.set("changes", Json::number(int64_t(changes)));
            r.set("summary", Json::string("Edited human " + name + " (" + std::to_string(changes) + " changes)"));
            return r;
        }});
    ws.registerOp({"human_validate", "Check a digital-human document (as written by hand) without saving: version 1, dna limits, "
        "rig corrections, skin weights, routes. Returns ok or the first error.",
        Json::parse(R"({"properties": {"document": {"type": "object"}}, "required": ["document"]})"),
        [](const Json& p) {
            vivid::HumanCreatorWorkbench human;
            std::string error;
            Json r = Json::object();
            const bool ok = human.loadDocument(p["document"], &error);
            r.set("ok", Json::fromBool(ok));
            if (!ok) r.set("error", Json::string(error));
            return r;
        }});
}

} // namespace aiverse::editor
