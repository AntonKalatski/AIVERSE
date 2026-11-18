// PHYSIS map authoring adapter, independent of game genre or native ECS state.
// Stores the PHYSIS block in the current map document, with ordinary MapDoc
// snapshot Undo/Redo. It is data authoring ONLY: native GameModule must opt in.
#pragma once
#include "core/MapDoc.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace aiverse::editor::physis_authoring {

inline const Json& read(const MapDoc& doc) {
    return doc.data.fps.isNull() ? doc.data.extra["physis"] :
           doc.data.fps["physis"];
}

inline Json defaultConfig() {
    Json v = Json::object();
    v.set("enabled", Json::fromBool(true));
    Json grid = Json::object();
    grid.set("originX", Json::number(int64_t(-16)));
    grid.set("originZ", Json::number(int64_t(-16)));
    grid.set("width", Json::number(int64_t(32)));
    grid.set("height", Json::number(int64_t(32)));
    grid.set("cellSizeMeters", Json::number(int64_t(128)));
    grid.set("seed", Json::number(int64_t(1)));
    v.set("grid", grid);
    Json climate = Json::object();
    climate.set("baseTemperature", Json::number(int64_t(15)));
    climate.set("baseHumidity", Json::numberText("0.5"));
    v.set("climate", climate);
    v.set("enableStorms", Json::fromBool(false));
    v.set("enableDisasters", Json::fromBool(false));
    v.set("enableWindZones", Json::fromBool(false));
    v.set("enableGravityFields", Json::fromBool(false));
    v.set("enableForces", Json::fromBool(false));
    v.set("floodMode", Json::string("LocalShallow"));
    return v;
}

// Strict bounded validation before beginning a MapDoc edit; rejects malformed
// data instead of applying it partially. Unknown keys are kept for future
// extensions (zones/events), and must be consumed by a project-owned loader.
inline bool validate(const Json& v, std::string* error = nullptr) {
    auto fail = [&](const char* reason) {
        if (error) *error = reason;
        return false;
    };
    if (!v.isObject()) return fail("PHYSIS block must be a JSON object");
    const auto& grid = v["grid"];
    const auto& climate = v["climate"];
    if (!grid.isObject() || !climate.isObject())
        return fail("PHYSIS grid and climate must be objects");
    const auto fieldInt = [](const Json& obj, const char* key,
                             int64_t minimum, int64_t maximum) {
        const auto& value = obj[key];
        if (!value.isNumber()) return false;
        const auto n = value.integer(std::numeric_limits<int64_t>::min());
        return n >= minimum && n <= maximum &&
               value.numberLiteral().find_first_of(".eE") == std::string::npos;
    };
    for (auto key : {"originX","originZ"}) {
        if (!fieldInt(grid, key, -1000000, 1000000))
            return fail("PHYSIS origin outside supported extent");
    }
    for (auto key : {"width","height"}) {
        if (!fieldInt(grid, key, 1, 1024))
            return fail("PHYSIS grid dimensions must be 1..1024");
    }
    if (grid.getInt("width") * grid.getInt("height") > 1048576)
        return fail("PHYSIS grid too large");
    if (!fieldInt(grid,"cellSizeMeters",1,100000))
        return fail("PHYSIS cellSizeMeters must be 1..100000");
    if (!fieldInt(grid,"seed",0,2147483647))
        return fail("PHYSIS seed must be nonnegative integer");
    const auto number = [](const Json& obj, const char* key,
                           double lo, double hi) {
        const auto& v = obj[key];
        if (!v.isNumber()) return false;
        const auto n = v.toDouble();
        return std::isfinite(n) && n >= lo && n <= hi;
    };
    if (!number(climate,"baseTemperature",-80,80))
        return fail("PHYSIS baseTemperature outside [-80,80]");
    if (!number(climate,"baseHumidity",0,1))
        return fail("PHYSIS baseHumidity outside [0,1]");
    for (auto key : {"enabled","enableStorms","enableDisasters",
                     "enableWindZones","enableGravityFields","enableForces"})
        if (!v[key].isNull() && v[key].type() != Json::Type::Bool)
            return fail("PHYSIS enable flags must be boolean");
    const auto mode = v.getStr("floodMode", "LocalShallow");
    if (mode != "LocalShallow" && mode != "ExternalAuthority")
        return fail("PHYSIS floodMode must be LocalShallow or ExternalAuthority");
    for (auto key : {"storms","windZones","gravityFields"})
        if (!v[key].isNull() && !v[key].isArray())
            return fail("PHYSIS authored field collections must be arrays");
    return true;
}

// An atomic document edit. The existing MapDoc undo history owns the snapshot;
// there is never an in-place mutation of a live Simulation.
inline bool apply(MapDoc& doc, const Json& config, std::string* error = nullptr) {
    if (!config.isNull() && !validate(config,error)) return false;
    if (!doc.data.fps.isNull() && !doc.data.fps.isObject()) {
        if (error) *error = "PHYSIS FPS document is not an object";
        return false;
    }
    const Json old = read(doc);
    if (old == config) return true;
    doc.beginEdit();
    Json& container = doc.data.fps.isNull() ? doc.data.extra : doc.data.fps;
    if (config.isNull()) container.erase("physis");
    else container.set("physis",config);
    return true;
}

} // namespace aiverse::editor::physis_authoring
