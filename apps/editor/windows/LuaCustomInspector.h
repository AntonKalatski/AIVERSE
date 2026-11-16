// AIVERSE Studio: sandboxed Lua 5.4 Custom Inspector / Property Drawer.
// Lua produces a declarative widget tree. C++ owns data, editing, validation and Undo.
// Scripts have no direct access to the filesystem, OS, gameplay world, or raw ImGui.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../Editor.h"
#include "StudioContextMenus.h"
#include "../EditorUtil.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace aiverse::editor {

class LuaCustomInspector {
    lua_State* L_ = nullptr;
    std::string file_, type_, source_, draft_, message_;
    std::filesystem::file_time_type stamp_{};
    std::map<std::string, std::string> textFields_;
    std::set<std::string> initialized_;
    bool editorOpen_ = false;
    bool applyAll_ = true;
    int lastEntity_ = -1;
    static constexpr int kMaxNodes = 384;
    static constexpr int kMaxDepth = 12;
    static constexpr int kMaxEdits = 128;
    static constexpr size_t kMaxSource = 128 * 1024;

    static void hook(lua_State* L, lua_Debug*) { luaL_error(L, "Lua Inspector exceeded instruction budget"); }
    static void budget(lua_State* L) { lua_sethook(L, hook, LUA_MASKCOUNT, 100000); }
    static void unbudget(lua_State* L) { lua_sethook(L, nullptr, 0, 0); }
    static std::string safeName(const std::string& type) {
        std::string out;
        for (unsigned char c : type) out += std::isalnum(c) || c == '_' || c == '-' ? char(c) : '_';
        return out.empty() ? "unnamed" : out;
    }
    static std::string field(lua_State* L, int index, const char* key, const char* fallback = "") {
        index = lua_absindex(L, index);
        lua_getfield(L, index, key);
        const char* v = lua_tostring(L, -1);
        std::string out = v ? v : fallback;
        lua_pop(L, 1);
        return out;
    }
    static bool flag(lua_State* L, int index, const char* key, bool fallback = false) {
        index = lua_absindex(L, index);
        lua_getfield(L, index, key);
        bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
        lua_pop(L, 1);
        return v;
    }
    static double num(lua_State* L, int index, const char* key, double fallback = 0) {
        index = lua_absindex(L, index);
        lua_getfield(L, index, key);
        double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
        lua_pop(L, 1);
        return std::isfinite(v) ? v : fallback;
    }
    static bool safePath(const std::string& path) {
        if (path.empty() || path.size() > 192) return false;
        for (unsigned char c : path) if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != '[' && c != ']') return false;
        if (path.find("..") != std::string::npos || path.front() == '.' || path.back() == '.') return false;
        return true;
    }
    struct Token { std::string key; int arrayIndex = -1; }; // Lua arrays: 1-based
    static bool tokenize(const std::string& path, std::vector<Token>& tokens) {
        if (!safePath(path)) return false;
        size_t i = 0;
        while (i < path.size()) {
            size_t start = i;
            while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
            if (start == i) return false;
            tokens.push_back({path.substr(start, i - start), -1});
            while (i < path.size() && path[i] == '[') {
                ++i;
                const size_t from = i;
                while (i < path.size() && std::isdigit(static_cast<unsigned char>(path[i]))) ++i;
                if (from == i || i >= path.size() || path[i] != ']') return false;
                int index = 0;
                for (size_t j = from; j < i; ++j) {
                    index = index * 10 + (path[j] - '0');
                    if (index > 4096) return false;
                }
                if (index < 1) return false;
                tokens.push_back({{}, index});
                ++i;
            }
            if (i < path.size()) {
                if (path[i++] != '.' || i == path.size()) return false;
            }
            if (tokens.size() > 32) return false;
        }
        return !tokens.empty();
    }
    static const Json& getPath(const Json& root, const std::string& path) {
        static const Json nil;
        std::vector<Token> tokens;
        if (!tokenize(path, tokens)) return nil;
        const Json* p = &root;
        for (auto& t : tokens) {
            if (t.arrayIndex > 0) {
                if (!p->isArray() || p->size() < size_t(t.arrayIndex)) return nil;
                p = &(*p)[size_t(t.arrayIndex - 1)];
            } else {
                if (!p->isObject()) return nil;
                p = &(*p)[t.key];
            }
        }
        return *p;
    }
    static bool setPath(Json& root, const std::string& path, const Json& value, std::string& error) {
        std::vector<Token> ts;
        if (!tokenize(path, ts)) { error = "Invalid property path: " + path; return false; }
        Json* dst = &root;
        for (size_t i = 0; i < ts.size(); ++i) {
            const bool last = i + 1 == ts.size();
            auto& t = ts[i];
            if (t.arrayIndex > 0) {
                if (!dst->isArray()) { error = "Property is not an array: " + path; return false; }
                if (size_t(t.arrayIndex) > dst->size()) { error = "Array index out of range: " + path; return false; }
                if (last) dst->at(size_t(t.arrayIndex - 1)) = value;
                else dst = &dst->at(size_t(t.arrayIndex - 1));
            } else {
                if (!dst->isObject()) { error = "Property is not an object: " + path; return false; }
                if (last) dst->set(t.key, value);
                else {
                    if (const Json* found = &(*dst)[t.key]; found->isNull()) {
                        dst->set(t.key, ts[i + 1].arrayIndex > 0 ? Json::array() : Json::object());
                    }
                    dst = &dst->member(t.key);
                }
            }
        }
        return true;
    }
    static Json luaJson(lua_State* L, int index, int depth = 0) {
        index = lua_absindex(L, index);
        if (depth > kMaxDepth) return {};
        if (lua_isboolean(L, index)) return Json::fromBool(lua_toboolean(L, index) != 0);
        if (lua_isinteger(L, index)) return Json::number(int64_t(lua_tointeger(L, index)));
        if (lua_isnumber(L, index)) {
            double v = lua_tonumber(L, index);
            return std::isfinite(v) ? Json::number(v, 4) : Json{};
        }
        if (lua_isstring(L, index)) return Json::string(lua_tostring(L, index));
        if (!lua_istable(L, index)) return {};
        const size_t n = lua_rawlen(L, index);
        bool isArray = n > 0;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || size_t(lua_tointeger(L, -2)) > n) isArray = false;
            lua_pop(L, 1);
        }
        if (isArray) {
            Json a = Json::array();
            for (size_t i = 1; i <= std::min(n, size_t(4096)); ++i) {
                lua_rawgeti(L, index, lua_Integer(i)); a.push(luaJson(L, -1, depth + 1)); lua_pop(L, 1);
            }
            return a;
        }
        Json object = Json::object();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char* k = lua_tostring(L, -2);
                if (k && std::strlen(k) <= 128) object.set(k, luaJson(L, -1, depth + 1));
            }
            lua_pop(L, 1);
        }
        return object;
    }
    static void pushJson(lua_State* L, const Json& value, int depth = 0) {
        if (depth > kMaxDepth) { lua_pushnil(L); return; }
        if (value.isString()) { const auto s = value.str(); lua_pushlstring(L, s.data(), s.size()); }
        else if (value.isNumber()) lua_pushnumber(L, value.toDouble());
        else if (value.type() == Json::Type::Bool) lua_pushboolean(L, value.boolean());
        else if (value.isObject()) {
            lua_newtable(L);
            for (const auto& [k, v] : value.members()) { pushJson(L, v, depth + 1); lua_setfield(L, -2, k.c_str()); }
        } else if (value.isArray()) {
            lua_newtable(L);
            for (size_t i = 0; i < value.size() && i < 4096; ++i) { pushJson(L, value[i], depth + 1); lua_rawseti(L, -2, lua_Integer(i + 1)); }
        } else lua_pushnil(L);
    }
    static void context(lua_State* L, const DocEntity& ent, size_t count) {
        lua_newtable(L);
        lua_pushstring(L, ent.type.c_str()); lua_setfield(L, -2, "type");
        lua_pushstring(L, ent.id.c_str()); lua_setfield(L, -2, "id");
        lua_pushinteger(L, ent.owner); lua_setfield(L, -2, "owner");
        lua_pushnumber(L, ent.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, ent.z); lua_setfield(L, -2, "z");
        lua_pushinteger(L, lua_Integer(count)); lua_setfield(L, -2, "selection_count");
        pushJson(L, ent.extra); lua_setfield(L, -2, "values");
    }
    void reset() {
        if (L_) lua_close(L_);
        L_ = nullptr;
        textFields_.clear(); initialized_.clear();
    }
    void reload() {
        reset(); source_.clear(); message_.clear();
        std::ifstream in(file_, std::ios::binary);
        if (!in) return;
        std::ostringstream contents; contents << in.rdbuf(); source_ = contents.str();
        if (source_.size() > kMaxSource) { message_ = "Lua script exceeds 128 KiB"; return; }
        L_ = luaL_newstate();
        if (!L_) { message_ = "Could not allocate Lua state"; return; }
        const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base}, {LUA_STRLIBNAME, luaopen_string},
            {LUA_TABLIBNAME, luaopen_table}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}};
        for (const auto& lib : libs) { luaL_requiref(L_, lib.name, lib.func, 1); lua_pop(L_, 1); }
        for (const char* k : {"dofile", "loadfile", "load", "collectgarbage"}) { lua_pushnil(L_); lua_setglobal(L_, k); }
        lua_getglobal(L_, "string"); lua_pushnil(L_); lua_setfield(L_, -2, "dump"); lua_pop(L_, 1);
        budget(L_);
        int err = luaL_loadbufferx(L_, source_.data(), source_.size(), file_.c_str(), "t");
        if (err == LUA_OK) err = lua_pcall(L_, 0, 0, 0);
        unbudget(L_);
        if (err != LUA_OK) { message_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Lua error"; reset(); }
    }
    std::vector<int> targets(Editor& ed) const {
        std::vector<int> out;
        const auto& d = ed.doc().data;
        if (applyAll_) for (int i : ed.selectedEntities())
            if (i >= 0 && size_t(i) < d.entities.size() && d.entities[size_t(i)].type == type_) out.push_back(i);
        if (out.empty() && ed.selection.kind == Selection::Entity && ed.selection.index >= 0 && size_t(ed.selection.index) < d.entities.size())
            out.push_back(ed.selection.index);
        return out;
    }
    struct Edit { std::string path; Json value; std::string op = "set"; };
    static bool editsFrom(lua_State* L, int index, std::vector<Edit>& result) {
        index = lua_absindex(L, index);
        if (!lua_istable(L, index)) return false;
        auto one = [&](int at) {
            const std::string path = field(L, at, "key", field(L, at, "path").c_str());
            if (!safePath(path)) return false;
            lua_getfield(L, at, "value");
            Edit e{path, luaJson(L, -1), field(L, at, "op", "set")};
            lua_pop(L, 1);
            if (e.op != "set" && e.op != "remove" && e.op != "append") return false;
            result.push_back(std::move(e));
            return true;
        };
        lua_getfield(L, index, "edits");
        if (lua_istable(L, -1)) {
            const size_t n = lua_rawlen(L, -1);
            if (n > kMaxEdits) { lua_pop(L, 1); return false; }
            for (size_t i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, lua_Integer(i));
                const bool ok = lua_istable(L, -1) && one(lua_gettop(L));
                lua_pop(L, 1);
                if (!ok) { lua_pop(L, 1); return false; }
            }
            lua_pop(L, 1);
            return !result.empty();
        }
        lua_pop(L, 1);
        return one(index);
    }
    bool runCallback(Editor& ed, int index, const char* function, const std::string& key,
                     const Json* newValue, std::vector<Edit>& edits) {
        if (!L_) return false;
        lua_getglobal(L_, function);
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return false; }
        context(L_, ed.doc().data.entities[size_t(index)], targets(ed).size());
        lua_pushstring(L_, key.c_str());
        int nargs = 2;
        if (newValue) { pushJson(L_, *newValue); ++nargs; }
        budget(L_);
        const int rc = lua_pcall(L_, nargs, 1, 0);
        unbudget(L_);
        if (rc != LUA_OK) {
            message_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Lua callback failed";
            lua_pop(L_, 1);
            edits.clear();
            return true;
        }
        if (lua_isstring(L_, -1) && std::string(lua_tostring(L_, -1)) == "cancel") {
            edits.clear();
        } else if (lua_istable(L_, -1)) {
            std::vector<Edit> replacement;
            if (editsFrom(L_, -1, replacement)) edits = std::move(replacement);
            else if (newValue) edits = {{key, luaJson(L_, -1), "set"}};
        } else if (newValue && !lua_isnil(L_, -1)) edits = {{key, luaJson(L_, -1), "set"}};
        lua_pop(L_, 1);
        return true;
    }
    static bool applyEdit(Json& object, const Edit& e, std::string& error) {
        if (e.op == "set") return setPath(object, e.path, e.value, error);
        std::vector<Token> tokens;
        if (!tokenize(e.path, tokens)) { error = "Invalid path: " + e.path; return false; }
        // Immutable lookup + mutable traversal: no partial changes to document (transaction uses a copy).
        Json* p = &object;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& t = tokens[i];
            const bool final = i + 1 == tokens.size();
            if (t.arrayIndex > 0) {
                if (!p->isArray() || p->size() < size_t(t.arrayIndex)) { error = "Array index out of range: " + e.path; return false; }
                if (final) { if (e.op != "remove") return false; p->eraseAt(size_t(t.arrayIndex - 1)); return true; }
                p = &p->at(size_t(t.arrayIndex - 1));
            } else {
                if (!p->isObject()) { error = "Expected object: " + e.path; return false; }
                if (final) {
                    if (e.op == "remove") { p->erase(t.key); return true; }
                    Json array = (*p)[t.key];
                    if (!array.isArray()) { if (!array.isNull()) { error = "Expected array: " + e.path; return false; } array = Json::array(); }
                    if (array.size() >= 4096) { error = "Array exceeds 4096 entries"; return false; }
                    array.push(e.value); p->set(t.key, std::move(array)); return true;
                }
                Json* found = p->find(t.key);
                if (!found) { error = "Missing property: " + e.path; return false; }
                p = found;
            }
        }
        return false;
    }
    void transact(Editor& ed, std::vector<Edit> edits) {
        if (ed.playing() || edits.empty()) return;
        auto ids = targets(ed);
        if (ids.empty() || edits.size() > kMaxEdits) return;
        auto& doc = ed.doc();
        std::vector<Json> next;
        next.reserve(ids.size());
        std::string error;
        for (int id : ids) {
            Json copy = doc.data.entities[size_t(id)].extra;
            if (!copy.isObject()) copy = Json::object();
            for (const auto& e : edits) if (!applyEdit(copy, e, error)) { message_ = error.empty() ? "Invalid edit" : error; return; }
            next.push_back(std::move(copy));
        }
        bool different = false;
        for (size_t i = 0; i < ids.size(); ++i) if (!(next[i] == doc.data.entities[size_t(ids[i])].extra)) different = true;
        if (!different) return;
        doc.beginEdit();
        for (size_t i = 0; i < ids.size(); ++i) doc.data.entities[size_t(ids[i])].extra = std::move(next[i]);
        doc.touched();
        textFields_.clear(); initialized_.clear();
        message_ = "Applied to " + std::to_string(ids.size()) + " object(s) · Undo available";
    }
    bool validate(Editor& ed, int index, const std::string& key, const Json& v) {
        if (!L_) return true;
        lua_getglobal(L_, "on_validate");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
        context(L_, ed.doc().data.entities[size_t(index)], targets(ed).size());
        lua_pushstring(L_, key.c_str());
        pushJson(L_, v);
        budget(L_);
        const int rc = lua_pcall(L_, 3, 1, 0);
        unbudget(L_);
        if (rc != LUA_OK) {
            message_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Validation failed";
            lua_pop(L_, 1);
            return false;
        }
        bool valid = !lua_isboolean(L_, -1) || lua_toboolean(L_, -1);
        if (lua_isstring(L_, -1)) { message_ = lua_tostring(L_, -1); valid = false; }
        if (!valid && message_.empty()) message_ = "Lua validation rejected the edit";
        lua_pop(L_, 1);
        return valid;
    }
    void change(Editor& ed, int index, const std::string& key, const Json& v) {
        if (!validate(ed, index, key, v)) return;
        std::vector<Edit> edits = {{key, v, "set"}};
        runCallback(ed, index, "on_change", key, &v, edits);
        transact(ed, std::move(edits));
    }
    void action(Editor& ed, int index, const std::string& name) {
        std::vector<Edit> edits;
        runCallback(ed, index, "on_action", name, nullptr, edits);
        transact(ed, std::move(edits));
    }
    static bool assetInProject(const std::filesystem::path& root, const std::filesystem::path& path) {
        if (path.empty() || path.is_absolute()) return false;
        for (const auto& p : path) if (p == "..") return false;
        std::error_code ec;
        return std::filesystem::is_regular_file(root / path, ec) && !ec;
    }
    void assetWidget(Editor& ed, int index, const std::string& key, const std::string& label, const Json& current) {
        auto base = std::filesystem::path(ed.ws().project.dir);
        if (!initialized_.count(key)) { textFields_[key] = current.str(); initialized_.insert(key); }
        auto& value = textFields_[key];
        ui::inputString((label + "##asset").c_str(), value);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (value.empty() || assetInProject(base, std::filesystem::path(value))) change(ed, index, key, Json::string(value));
            else message_ = "Asset must be an existing project-relative file";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("...##pick")) ImGui::OpenPopup("##asset_pick");
        if (ImGui::BeginPopup("##asset_pick")) {
            ImGui::TextDisabled("Select project asset");
            std::error_code ec;
            auto iter = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
            const auto end = std::filesystem::recursive_directory_iterator();
            int visited = 0, shown = 0;
            for (; !ec && iter != end && visited < 1200 && shown < 140; iter.increment(ec), ++visited) {
                if (iter.depth() > 6) { iter.disable_recursion_pending(); continue; }
                if (!iter->is_regular_file(ec) || iter->is_symlink(ec)) continue;
                const auto relative = std::filesystem::relative(iter->path(), base, ec);
                if (ec) break;
                const std::string option = relative.generic_string();
                if (option.size() > 220) continue;
                if (ImGui::Selectable(option.c_str(), option == value)) {
                    change(ed, index, key, Json::string(option));
                    ImGui::CloseCurrentPopup();
                }
                ++shown;
            }
            if (!shown) ImGui::TextDisabled("No assets found");
            ImGui::EndPopup();
        }
    }
    bool widget(Editor& ed, int index, int at, int& budgetNodes, int depth, const std::string& prefix = "") {
        if (++budgetNodes > kMaxNodes || depth > kMaxDepth) return false;
        at = lua_absindex(L_, at);
        const std::string kind = field(L_, at, "kind", "label");
        const std::string rawKey = field(L_, at, "key");
        const std::string key = prefix.empty() ? rawKey : rawKey.empty() ? prefix : prefix + "." + rawKey;
        const std::string label = field(L_, at, "label", rawKey.c_str());
        if (flag(L_, at, "hidden")) return true;
        ImGui::PushID(budgetNodes);
        if (kind == "label" || kind == "help") {
            if (kind == "help") ImGui::TextDisabled("%s", label.c_str());
            else ImGui::TextWrapped("%s", label.c_str());
        } else if (kind == "spacer") ImGui::Spacing();
        else if (kind == "separator") ImGui::SeparatorText(label.c_str());
        else if (kind == "group" || kind == "foldout" || kind == "row") {
            bool show = kind == "row" || ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (show) {
                if (kind == "row") ImGui::BeginGroup();
                lua_getfield(L_, at, "children");
                if (lua_istable(L_, -1)) {
                    const size_t n = std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(kMaxNodes));
                    for (size_t i = 1; i <= n && budgetNodes < kMaxNodes; ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        if (lua_istable(L_, -1)) widget(ed, index, -1, budgetNodes, depth + 1, key);
                        lua_pop(L_, 1);
                        if (kind == "row" && i < n) ImGui::SameLine();
                    }
                }
                lua_pop(L_, 1);
                if (kind == "row") ImGui::EndGroup();
            }
        } else if (kind == "button") {
            if (ImGui::Button(label.c_str())) action(ed, index, rawKey.empty() ? label : rawKey);
        } else if (!safePath(key)) {
            ImGui::TextDisabled("Invalid property path: %s", key.c_str());
        } else {
            const Json& current = getPath(ed.doc().data.entities[size_t(index)].extra, key);
            bool readOnly = flag(L_, at, "readonly", false) || ed.playing();
            ImGui::BeginDisabled(readOnly);
            if (kind == "checkbox" || kind == "bool") {
                bool v = current.boolean(flag(L_, at, "default"));
                if (ImGui::Checkbox(label.c_str(), &v)) change(ed, index, key, Json::fromBool(v));
            } else if (kind == "slider" || kind == "number" || kind == "int") {
                float v = float(current.toDouble(num(L_, at, "default")));
                const float lo = float(num(L_, at, "min")), hi = float(num(L_, at, "max", 100));
                bool edited = kind == "slider" ? ImGui::SliderFloat(label.c_str(), &v, lo, std::max(lo, hi)) : ImGui::DragFloat(label.c_str(), &v, 0.1f);
                if (edited && std::isfinite(v)) change(ed, index, key, kind == "int" ? Json::number(int64_t(std::llround(v))) : Json::number(double(v), 4));
            } else if (kind == "text" || kind == "multiline" || kind == "asset") {
                if (kind == "asset") assetWidget(ed, index, key, label, current);
                else {
                    if (!initialized_.count(key)) { textFields_[key] = current.str(); initialized_.insert(key); }
                    auto& v = textFields_[key];
                    bool edited = kind == "multiline" ? ui::inputMultiline((label + "##lua").c_str(), v, ImVec2(-1, 90)) : ui::inputString((label + "##lua").c_str(), v);
                    (void)edited;
                    if (ImGui::IsItemDeactivatedAfterEdit()) change(ed, index, key, Json::string(v));
                }
            } else if (kind == "enum" || kind == "dropdown") {
                std::string value = current.str();
                if (ImGui::BeginCombo(label.c_str(), value.c_str())) {
                    lua_getfield(L_, at, "options");
                    if (lua_istable(L_, -1)) for (size_t i = 1; i <= std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(128)); ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        const std::string option = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : lua_istable(L_, -1) ? field(L_, -1, "value") : std::string();
                        if (ImGui::Selectable(option.c_str(), value == option)) change(ed, index, key, Json::string(option));
                        lua_pop(L_, 1);
                    }
                    lua_pop(L_, 1); ImGui::EndCombo();
                }
            } else if (kind == "vector2" || kind == "vector3" || kind == "vector4" || kind == "color") {
                const int n = kind == "vector2" ? 2 : kind == "vector3" ? 3 : 4;
                float vec[4] = {0, 0, 0, 1};
                for (int i = 0; i < n; ++i) if (current.isArray() && size_t(i) < current.size()) vec[i] = float(current[size_t(i)].toDouble());
                bool changed = kind == "color" ? ImGui::ColorEdit4(label.c_str(), vec) : n == 2 ? ImGui::DragFloat2(label.c_str(), vec, 0.1f) : n == 3 ? ImGui::DragFloat3(label.c_str(), vec, 0.1f) : ImGui::DragFloat4(label.c_str(), vec, 0.1f);
                if (changed) {
                    Json arr = Json::array();
                    for (int i = 0; i < n; ++i) arr.push(Json::number(double(vec[i]), 4));
                    change(ed, index, key, arr);
                }
            } else if (kind == "array") {
                const Json& array = current;
                ImGui::Text("%s (%zu)", label.c_str(), array.isArray() ? array.size() : 0u);
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    lua_getfield(L_, at, "default"); Json d = luaJson(L_, -1); lua_pop(L_, 1);
                    transact(ed, {{key, d, "append"}});
                }
                if (array.isArray()) for (size_t i = 0; i < array.size() && i < 128; ++i) {
                    ImGui::PushID(int(i));
                    const std::string childPath = key + "[" + std::to_string(i + 1) + "]";
                    const bool open = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_SpanAvailWidth, "[%zu]", i + 1);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) transact(ed, {{childPath, {}, "remove"}});
                    if (open) {
                        lua_getfield(L_, at, "element");
                        if (lua_istable(L_, -1)) {
                            // Child widgets inherit element's path, e.g. inventory[1].count.
                            widget(ed, index, -1, budgetNodes, depth + 1, childPath);
                        } else {
                            ImGui::TextDisabled("%s", array[i].dump(-1).c_str());
                        }
                        lua_pop(L_, 1);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("Unsupported inspector widget: %s", kind.c_str());
            }
            ImGui::EndDisabled();
        }
        const std::string tooltip = field(L_, at, "tooltip");
        if (!tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
        if (StudioContextMenus::get().available(ed) &&
            ImGui::BeginPopupContextItem("##inspector_property_context")) {
            StudioContextMenus::get().draw(ed, "inspector.property", key.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return true;
    }

public:
    ~LuaCustomInspector() { reset(); }
    void draw(Editor& ed) {
        if (ed.ws().project.dir.empty() || ed.selection.kind != Selection::Entity ||
            ed.selection.index < 0 || size_t(ed.selection.index) >= ed.doc().data.entities.size()) return;
        const auto& ent = ed.doc().data.entities[size_t(ed.selection.index)];
        const std::string type = safeName(ent.type);
        const auto path = (std::filesystem::path(ed.ws().project.dir) / "editor" / "inspectors" / (type + ".lua")).string();
        if (file_ != path) { file_ = path; type_ = ent.type; draft_.clear(); source_.clear(); stamp_ = {}; editorOpen_ = false; reset(); }
        if (lastEntity_ != ed.selection.index) { lastEntity_ = ed.selection.index; textFields_.clear(); initialized_.clear(); }
        std::error_code ec;
        auto now = std::filesystem::last_write_time(file_, ec);
        if (ec) now = {};
        if (now != stamp_) { stamp_ = now; reload(); draft_ = source_; }
        ImGui::Separator();
        ui::section(ICON_BRACES, "Lua Custom Inspector");
        ImGui::TextDisabled("%s", ("editor/inspectors/" + type + ".lua").c_str());
        if (ed.selectedEntities().size() > 1) {
            ImGui::Checkbox("Редактировать все выбранные объекты этого типа", &applyAll_);
            ImGui::TextDisabled("%zu выбранных", targets(ed).size());
        }
        if (ui::button(ICON_PENCIL, editorOpen_ ? "Скрыть код" : "Редактировать Lua")) editorOpen_ = !editorOpen_;
        ImGui::SameLine();
        if (ui::toolButton(ICON_REFRESH, "Перезагрузить Lua")) { reload(); draft_ = source_; }
        if (!message_.empty()) ui::callout(ui::Tone::Warn, message_);
        if (L_) {
            lua_getglobal(L_, "inspector");
            if (lua_isfunction(L_, -1)) {
                context(L_, ent, targets(ed).size());
                budget(L_); const int rc = lua_pcall(L_, 1, 1, 0); unbudget(L_);
                if (rc == LUA_OK && lua_istable(L_, -1)) {
                    int count = 0;
                    size_t n = std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(kMaxNodes));
                    for (size_t i = 1; i <= n && count < kMaxNodes; ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        if (lua_istable(L_, -1)) widget(ed, ed.selection.index, -1, count, 0);
                        lua_pop(L_, 1);
                    }
                } else if (rc != LUA_OK) message_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Inspector error";
                lua_pop(L_, 1);
            } else lua_pop(L_, 1);
        } else if (!editorOpen_) ui::hint("Для этого типа ещё нет Lua-инспектора. Нажмите «Редактировать Lua».");
        if (editorOpen_) {
            if (draft_.empty() && source_.empty()) draft_ =
                "function inspector(ctx)\n  return {\n"
                "    {kind='group', label='Gameplay', children={\n"
                "      {kind='slider', key='stats.health', label='Health', min=0, max=100},\n"
                "      {kind='checkbox', key='enabled', label='Enabled'},\n"
                "      {kind='enum', key='ai.mode', label='AI Mode', options={'Idle','Patrol','Attack'}},\n"
                "      {kind='asset', key='model', label='Model path'},\n"
                "      {kind='array', key='inventory', label='Inventory', default={name='New', count=1},\n"
                "        element={kind='group', label='Item', children={\n"
                "          {kind='text', key='name', label='Name'},\n"
                "          {kind='int', key='count', label='Count'}\n"
                "        }}}\n"
                "    }},\n    {kind='button', key='reset', label='Reset Health'}\n  }\nend\n\n"
                "function on_action(ctx, id)\n  if id == 'reset' then\n"
                "    return {edits={{key='stats.health',value=100},{key='enabled',value=true}}}\n"
                "  end\nend\n";
            ui::inputMultiline("##studio_lua_source", draft_, ImVec2(-1, 280));
            if (ui::button(ICON_SAVE, "Сохранить Lua")) {
                if (draft_.size() > kMaxSource) { message_ = "Lua script exceeds 128 KiB"; return; }
                std::filesystem::create_directories(std::filesystem::path(file_).parent_path(), ec);
                if (ec) { message_ = ec.message(); return; }
                std::ofstream out(file_, std::ios::binary | std::ios::trunc);
                if (!out) { message_ = "Could not open script for writing"; return; }
                out << draft_; out.close();
                if (!out) { message_ = "Lua file save failed"; return; }
                reload(); draft_ = source_;
                stamp_ = std::filesystem::last_write_time(file_, ec);
            }
        }
    }
};

} // namespace aiverse::editor
