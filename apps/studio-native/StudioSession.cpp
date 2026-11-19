#include "StudioSession.h"
#include <cctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aiverse::studio {
namespace {
Json str(const std::string& s) { return Json::string(s); }
Json num(int64_t n) { return Json::number(n); }
Json prop(const char* key, const char* label, const char* type, Json value) {
    Json r = Json::object();
    r.set("key", str(key)); r.set("label", str(label));
    r.set("type", str(type)); r.set("value", std::move(value));
    return r;
}
}
StudioSession::StudioSession(const std::string& engineRoot, const std::string& project) {
    ws_.engineDir = engineRoot;
    if (!project.empty()) {
        std::string error;
        if (!ws_.openGame(project, &error)) throw std::runtime_error(error.empty() ? "Cannot open project" : error);
    }
}
int StudioSession::indexFor(const std::string& id) const {
    if (id.compare(0, 7, "entity:") != 0 || id.size() == 7) throw std::runtime_error("Invalid entity ID");
    uint64_t n = 0;
    for (size_t i = 7; i < id.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(id[i]);
        if (!std::isdigit(c)) throw std::runtime_error("Invalid entity ID");
        const uint64_t digit = uint64_t(c - '0');
        const uint64_t limit = uint64_t(std::numeric_limits<int>::max());
        if (n > (limit - digit) / 10) throw std::runtime_error("Entity index overflow");
        n = n * 10 + digit;
    }
    if (n >= ws_.map.data.entities.size()) throw std::runtime_error("Entity does not exist");
    return int(n);
}
Json StudioSession::snapshot() const {
    Json result = Json::object();
    Json project = Json::object();
    project.set("name", str(ws_.project.manifest.getStr("name", "No project opened")));
    project.set("path", str(ws_.project.dir));
    project.set("dirty", Json::fromBool(ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty));
    result.set("project", std::move(project));
    Json scene = Json::object();
    scene.set("name", str(ws_.map.data.name));
    Json rows = Json::array();
    for (size_t i = 0; i < ws_.map.data.entities.size(); ++i) {
        const auto& e = ws_.map.data.entities[i];
        Json row = Json::object();
        row.set("id", str("entity:" + std::to_string(i)));
        row.set("name", str(e.type + " #" + std::to_string(i + 1)));
        row.set("kind", str(e.type));
        row.set("visible", Json::fromBool(true));
        rows.push(std::move(row));
    }
    scene.set("entities", std::move(rows));
    result.set("scene", std::move(scene));
    Json fields = Json::array();
    bool valid = false;
    if (!selected_.empty()) {
        try {
            const auto& e = ws_.map.data.entities.at(size_t(indexFor(selected_)));
            valid = true;
            fields.push(prop("type", "Type", "text", str(e.type)));
            fields.push(prop("x", "Position X", "number", Json::number(double(e.x), 4)));
            fields.push(prop("z", "Position Z", "number", Json::number(double(e.z), 4)));
            fields.push(prop("owner", "Owner", "number", num(int64_t(e.owner))));
        } catch (const std::exception&) {}
    }
    result.set("selectedId", valid ? str(selected_) : Json());
    result.set("properties", std::move(fields));
    Json logs = Json::array();
    const auto& activity = ws_.activity;
    for (size_t i = activity.size() > 100 ? activity.size() - 100 : 0; i < activity.size(); ++i) {
        Json row = Json::object();
        row.set("level", str(activity[i].error ? "error" : "info"));
        row.set("text", str(activity[i].text));
        row.set("time", str(activity[i].source));
        logs.push(std::move(row));
    }
    result.set("logs", std::move(logs));
    Json history = Json::object();
    history.set("canUndo", Json::fromBool(ws_.map.canUndo()));
    history.set("canRedo", Json::fromBool(ws_.map.canRedo()));
    result.set("history", std::move(history));
    result.set("mode", str("edit"));
    return result;
}
Json StudioSession::openProject(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Project path is empty");
    std::string error;
    if (!ws_.openGame(path, &error))
        throw std::runtime_error(error.empty() ? "Unable to open project" : error);
    selected_.clear();
    return snapshot();
}
int StudioSession::selectedEntityIndex() const {
    if (selected_.empty()) return -1;
    try { return indexFor(selected_); }
    catch (const std::exception&) { return -1; }
}
Json StudioSession::dispatch(const Json& request) {
    Json reply = Json::object();
    reply.set("id", request["id"]);
    try {
        if (!request.isObject() || !request["id"].isNumber()) throw std::runtime_error("Invalid request envelope");
        const std::string method = request.getStr("method");
        const Json& params = request["params"];
        if (method == "studio.snapshot") {
            reply.set("result", snapshot());
        } else if (method == "studio.select") {
            const Json& id = params["id"];
            if (!id.isNull() && !id.isString()) throw std::runtime_error("Invalid selection");
            const std::string next = id.isNull() ? "" : id.str();
            if (!next.empty()) indexFor(next);
            selected_ = next;
            reply.set("result", snapshot());
        } else if (method == "studio.save") {
            const Json r = ws_.call("map_save", Json::object(), "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            reply.set("result", snapshot());
        } else if (method == "studio.undo") {
            if (!ws_.map.canUndo()) throw std::runtime_error("Nothing to undo");
            ws_.map.undo();
            reply.set("result", snapshot());
        } else if (method == "studio.redo") {
            if (!ws_.map.canRedo()) throw std::runtime_error("Nothing to redo");
            ws_.map.redo();
            reply.set("result", snapshot());
        } else if (method == "studio.setProperty") {
            const std::string entityId = params.getStr("entityId");
            if (selected_.empty() || selected_ != entityId) throw std::runtime_error("Selection changed while editing");
            const int index = indexFor(entityId);
            const std::string key = params.getStr("key");
            const Json& value = params["value"];
            if (key == "type") {
                if (!value.isString() || value.str().empty()) throw std::runtime_error("Invalid type");
            } else if (key == "x" || key == "z" || key == "owner") {
                if (!value.isNumber()) throw std::runtime_error("Expected number");
                const double v = value.toDouble();
                if (!(v >= -1000000 && v <= 1000000)) throw std::runtime_error("Value out of range");
                if (key == "owner" && (v != double(value.integer()) || v < -1 || v > 10000))
                    throw std::runtime_error("Invalid owner");
            } else throw std::runtime_error("Property not editable");
            Json changes = Json::object(); changes.set(key, value);
            Json op = Json::object();
            op.set("index", num(index));
            op.set("set", std::move(changes));
            const Json r = ws_.call("entity_update", op, "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            reply.set("result", snapshot());
        } else throw std::runtime_error("Unknown Studio method");
    } catch (const std::exception& ex) {
        reply.set("error", str(ex.what()));
    }
    return reply;
}
}
