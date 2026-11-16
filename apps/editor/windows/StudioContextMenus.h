// AIVERSE Studio - safe, project-local Lua context menu contributions.
// Each native editor window owns its popup lifetime; this class only contributes items.
#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../Editor.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace aiverse::editor {
class StudioContextMenus {
public:
    static StudioContextMenus& get() { static StudioContextMenus s; return s; }
    StudioContextMenus(const StudioContextMenus&) = delete;
    StudioContextMenus& operator=(const StudioContextMenus&) = delete;
    ~StudioContextMenus() { reset(); }

    bool available(Editor& ed) {
        if (ed.ws().project.dir.empty()) return false;
        load(ed);
        if (!L_) return false;
        lua_getglobal(L_, "context_menu");
        const bool found = lua_isfunction(L_, -1);
        lua_pop(L_, 1);
        return found;
    }

    // Called INSIDE an already opened ImGui popup, including a nested native popup.
    void draw(Editor& ed, const char* window, const char* element = "") {
        if (ed.ws().project.dir.empty()) return;
        load(ed);
        if (!L_) return;
        lua_getglobal(L_, "context_menu");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }
        pushContext(ed, window, element);
        if (!call(1, 1)) return;
        if (lua_istable(L_, -1)) {
            const int root = lua_absindex(L_, -1);
            const int n = std::min<int>(int(lua_rawlen(L_, root)), 128);
            if (n) ImGui::Separator();
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L_, root, i);
                if (lua_istable(L_, -1)) drawItem(ed, lua_gettop(L_), window, element, 0);
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);
    }

    // Default context menu for arbitrary ImGui widgets/windows. No popup on the
    // Scene viewport: RMB is reserved for camera rotation there.
    void drawWindowFallback(Editor& ed, const char* window) {
        if (std::string(window) == "scene" || std::string(window) == "hierarchy" || !available(ed)) return;
        // Native popup handlers run inside w->draw() first; don't steal their
        // right click. Draw our own popup on controls without native menus.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            ImGui::OpenPopup("##aiverse_lua_context");
        if (ImGui::BeginPopup("##aiverse_lua_context")) {
            draw(ed, window);
            ImGui::EndPopup();
        }
    }

private:
    StudioContextMenus() = default;
    lua_State* L_ = nullptr;
    std::string project_, path_, error_;
    std::filesystem::file_time_type stamp_{};
    static void budget(lua_State* L, lua_Debug*) { luaL_error(L, "context menu instruction budget exceeded"); }
    bool call(int nargs, int nret) {
        lua_sethook(L_, budget, LUA_MASKCOUNT, 50000);
        const int rc = lua_pcall(L_, nargs, nret, 0);
        lua_sethook(L_, nullptr, 0, 0);
        if (rc == LUA_OK) return true;
        error_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "Lua context menu error";
        lua_pop(L_, 1);
        return false;
    }
    void reset() { if (L_) lua_close(L_); L_ = nullptr; project_.clear(); path_.clear(); error_.clear(); }
    void load(Editor& ed) {
        namespace fs = std::filesystem;
        const fs::path file = fs::path(ed.ws().project.dir) / "editor" / "context_menus.lua";
        std::error_code ec;
        const auto ts = fs::last_write_time(file, ec);
        const bool missing = bool(ec);
        if (project_ == ed.ws().project.dir && path_ == file.string() && ((missing && !L_) || (!missing && stamp_ == ts))) return;
        reset(); project_ = ed.ws().project.dir; path_ = file.string();
        if (missing) return;
        stamp_ = ts;
        if (fs::file_size(file, ec) > 128 * 1024 || ec) { error_ = "context menu script >128 KiB"; return; }
        std::ifstream input(file, std::ios::binary);
        const std::string src((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        L_ = luaL_newstate();
        if (!L_) { error_ = "Lua state allocation failed"; return; }
        const luaL_Reg libs[] = {{"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
                                  {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_UTF8LIBNAME, luaopen_utf8}};
        for (const auto& lib : libs) { luaL_requiref(L_, lib.name, lib.func, 1); lua_pop(L_, 1); }
        for (const char* name : {"dofile", "loadfile", "load", "collectgarbage"}) { lua_pushnil(L_); lua_setglobal(L_, name); }
        if (luaL_loadbufferx(L_, src.c_str(), src.size(), file.string().c_str(), "t") != LUA_OK) {
            error_ = lua_tostring(L_, -1); lua_pop(L_, 1);
        } else call(0, 0);
    }
    void pushContext(Editor& ed, const char* window, const char* element) {
        lua_newtable(L_);
        lua_pushstring(L_, window); lua_setfield(L_, -2, "window");
        lua_pushstring(L_, element); lua_setfield(L_, -2, "element");
        lua_pushinteger(L_, int(ed.selection.kind)); lua_setfield(L_, -2, "selection_kind");
        lua_pushinteger(L_, ed.selection.index); lua_setfield(L_, -2, "selection_index");
        lua_pushinteger(L_, int(ed.selected().size())); lua_setfield(L_, -2, "selection_count");
        lua_pushboolean(L_, ed.playing()); lua_setfield(L_, -2, "playing");
        if (ed.selection.kind == Selection::Entity && ed.selection.index >= 0 &&
            ed.selection.index < int(ed.doc().data.entities.size())) {
            const auto& entity = ed.doc().data.entities[size_t(ed.selection.index)];
            lua_pushstring(L_, entity.type.c_str()); lua_setfield(L_, -2, "entity_type");
            lua_pushstring(L_, entity.id.c_str()); lua_setfield(L_, -2, "entity_id");
        }
    }
    static std::string str(lua_State* L, int t, const char* name) {
        t = lua_absindex(L, t); lua_getfield(L, t, name);
        const char* value = lua_tostring(L, -1); std::string s = value ? value : "";
        lua_pop(L, 1); return s;
    }
    bool flag(int t, const char* name, bool fallback) {
        t = lua_absindex(L_, t); lua_getfield(L_, t, name);
        const bool r = lua_isboolean(L_, -1) ? lua_toboolean(L_, -1) != 0 : fallback;
        lua_pop(L_, 1); return r;
    }
    void run(Editor& ed, const std::string& id, const char* window, const char* element) {
        lua_getglobal(L_, "on_context_action");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }
        pushContext(ed, window, element);
        lua_pushlstring(L_, id.data(), id.size());
        if (!call(2, 1)) { ed.status = "Lua context: " + error_; return; }
        if (lua_istable(L_, -1)) {
            int t = lua_absindex(L_, -1);
            const std::string command = str(L_, t, "command");
            if (command == "focus_selection") {
                float x, z;
                if (ed.selectionPos(ed.selection, x, z)) ed.focusScene(x, z, 18);
            } else if (command == "open_window") {
                const std::string target = str(L_, t, "id");
                // Only registered windows, never unrestricted native actions.
                if (!target.empty() && target.size() <= 64) ed.focusWindow(target);
            } else if (command == "clear_selection") ed.select({});
            else if (command == "select_entity") {
                lua_getfield(L_, t, "index");
                const int index = lua_isinteger(L_, -1) ? int(lua_tointeger(L_, -1)) : -1;
                lua_pop(L_, 1);
                if (index >= 0 && index < int(ed.doc().data.entities.size())) ed.selectEntity(index);
            } else if (command == "status") {
                const std::string message = str(L_, t, "text");
                if (message.size() <= 512) ed.status = message;
            } else if (command == "undo" && !ed.playing()) ed.doc().undo();
            else if (command == "redo" && !ed.playing()) ed.doc().redo();
            else if (command == "set_entity_extra" && !ed.playing()) {
                // Deliberately limited to one top-level custom data key. This is not
                // arbitrary code execution or raw access to engine objects.
                const std::string key = str(L_, t, "key");
                if (key.empty() || key.size() > 128 || key.find_first_of("/\\[]") != std::string::npos) {
                    ed.status = "Lua context: invalid extra key";
                } else {
                    lua_getfield(L_, t, "value");
                    const int value = lua_absindex(L_, -1);
                    Json j;
                    if (lua_isboolean(L_, value)) j = Json::fromBool(lua_toboolean(L_, value) != 0);
                    else if (lua_isnumber(L_, value)) j = Json::number(double(lua_tonumber(L_, value)));
                    else if (lua_isstring(L_, value)) j = Json::string(lua_tostring(L_, value));
                    else if (lua_isnil(L_, value)) j = Json();
                    lua_pop(L_, 1);
                    if (!j.isNull()) {
                        auto indices = ed.selectedEntities();
                        if (!indices.empty()) {
                            ed.doc().beginEdit();
                            for (int i : indices) ed.doc().data.entities[size_t(i)].extra.set(key, j);
                            ed.doc().touched();
                            ed.status = "Lua context: updated " + key;
                        }
                    }
                }
            }
        }
        lua_pop(L_, 1);
    }
    void drawItem(Editor& ed, int t, const char* window, const char* element, int depth) {
        if (depth > 8) return;
        t = lua_absindex(L_, t);
        if (!flag(t, "visible", true)) return;
        std::string label = str(L_, t, "label"), id = str(L_, t, "id");
        if (label.empty() && id.empty()) { ImGui::Separator(); return; }
        if (label.empty() || label.size() > 160 || id.size() > 128) return;
        const std::string scope = str(L_, t, "scope");
        if (!scope.empty() && scope != "*" && scope != window) return;
        const std::string item = str(L_, t, "element");
        if (!item.empty() && item != element) return;
        const bool enabled = flag(t, "enabled", true), checked = flag(t, "checked", false);
        const std::string shortcut = str(L_, t, "shortcut");
        lua_getfield(L_, t, "children");
        const bool submenu = lua_istable(L_, -1) && lua_rawlen(L_, -1) > 0;
        lua_pop(L_, 1);
        if (submenu) {
            if (ImGui::BeginMenu(label.c_str(), enabled)) {
                lua_getfield(L_, t, "children");
                const int children = lua_absindex(L_, -1);
                const int count = std::min<int>(int(lua_rawlen(L_, children)), 96);
                for (int i = 1; i <= count; ++i) {
                    lua_rawgeti(L_, children, i);
                    if (lua_istable(L_, -1)) drawItem(ed, lua_gettop(L_), window, element, depth + 1);
                    lua_pop(L_, 1);
                }
                lua_pop(L_, 1);
                ImGui::EndMenu();
            }
        } else if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, enabled)) {
            if (!id.empty()) run(ed, id, window, element);
        }
    }
};
} // namespace aiverse::editor
