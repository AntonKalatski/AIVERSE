// AIVERSE Studio: project-local, declarative Lua editor extensions.
// A project supplies editor/extensions.lua. Lua never receives ImGui or raw engine pointers.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
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

class StudioLuaExtensions {
public:
    static StudioLuaExtensions& get() { static StudioLuaExtensions ext; return ext; }
    StudioLuaExtensions(const StudioLuaExtensions&) = delete;
    StudioLuaExtensions& operator=(const StudioLuaExtensions&) = delete;
    ~StudioLuaExtensions() { reset(); }

    void drawPanel(Editor& ed) {
        if (ed.ws().project.dir.empty()) { ImGui::TextDisabled("Откройте проект, чтобы использовать Lua Editor API."); return; }
        load(ed);
        ImGui::TextDisabled("Project / editor/extensions.lua");
        ImGui::SameLine();
        if (ImGui::SmallButton("Создать пример")) createExample(ed);
        ImGui::SameLine();
        if (ImGui::SmallButton("Перезагрузить")) { reset(); load(ed); }
        if (!error_.empty()) { ImGui::TextWrapped("Lua: %s", error_.c_str()); return; }
        if (!L_) { ImGui::TextDisabled("Создайте editor/extensions.lua или нажмите «Создать пример»."); return; }
        lua_getglobal(L_, "window");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); ImGui::TextDisabled("Определите function window(ctx) ... end"); return; }
        pushCtx(ed);
        if (!invoke(1, 1)) return;
        if (lua_istable(L_, -1)) {
            int table = lua_absindex(L_, -1);
            const int n = std::min<int>(int(lua_rawlen(L_, table)), 128);
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L_, table, i);
                if (lua_istable(L_, -1)) drawWidget(ed, lua_gettop(L_), i);
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);
    }

    void drawScene(Editor& ed, const Viewport& vp) {
        if (!vp.color.valid() || vp.pointsW < 1 || vp.pointsH < 1 || ed.ws().project.dir.empty()) return;
        load(ed);
        if (!L_) return;
        lua_getglobal(L_, "scene_gizmos");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }
        pushCtx(ed);
        if (!invoke(1, 1)) return;
        if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
        const int tbl = lua_absindex(L_, -1);
        const int count = std::min<int>(int(lua_rawlen(L_, tbl)), 256);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(vp.originX, vp.originY), ImVec2(vp.originX + vp.pointsW, vp.originY + vp.pointsH), true);
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L_, tbl, i);
            if (!lua_istable(L_, -1)) { lua_pop(L_, 1); continue; }
            const int entry = lua_absindex(L_, -1);
            const std::string kind = field(entry, "kind");
            const auto byte = [&](const char* key, double fallback) { return int(std::clamp(num(entry, key, fallback), 0.0, 1.0) * 255); };
            const ImU32 color = IM_COL32(byte("r", 0.35), byte("g", 0.70), byte("b", 1.0), 240);
            const float weight = float(std::clamp(num(entry, "thickness", 2), 1.0, 10.0));
            ImVec2 p, q;
            if (project(ed, vp, entry, "x", "y", "z", p)) {
                if (kind == "line" && project(ed, vp, entry, "x2", "y2", "z2", q)) dl->AddLine(p, q, color, weight);
                else if (kind == "circle") dl->AddCircle(p, float(std::clamp(num(entry, "radius", 12), 1.0, 120.0)), color, 32, weight);
                else if (kind == "point") dl->AddCircleFilled(p, float(std::clamp(num(entry, "radius", 4), 1.0, 20.0)), color);
                else if (kind == "label") { const std::string label = field(entry, "text"); if (label.size() < 200) dl->AddText(p, color, label.c_str()); }
            }
            lua_pop(L_, 1);
        }
        dl->PopClipRect();
        lua_pop(L_, 1);
    }

private:
    StudioLuaExtensions() = default;
    lua_State* L_ = nullptr;
    std::string path_, project_, error_;
    std::filesystem::file_time_type timestamp_{};
    std::map<std::string, std::string> textState_;
    std::map<std::string, double> numberState_;
    std::map<std::string, bool> boolState_;

    void reset() { if (L_) lua_close(L_); L_ = nullptr; path_.clear(); project_.clear(); error_.clear(); textState_.clear(); numberState_.clear(); boolState_.clear(); }
    static void hook(lua_State* L, lua_Debug*) { luaL_error(L, "Lua editor extension exceeded instruction budget"); }
    bool invoke(int nargs, int nret) {
        lua_sethook(L_, hook, LUA_MASKCOUNT, 100000);
        const int result = lua_pcall(L_, nargs, nret, 0);
        lua_sethook(L_, nullptr, 0, 0);
        if (result == LUA_OK) return true;
        error_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "Lua runtime error";
        lua_pop(L_, 1);
        return false;
    }
    void load(Editor& ed) {
        namespace fs = std::filesystem;
        const auto dir = fs::path(ed.ws().project.dir);
        const auto script = dir / "editor" / "extensions.lua";
        std::error_code ec;
        const auto stamp = fs::last_write_time(script, ec);
        const bool missing = bool(ec);
        if (project_ == ed.ws().project.dir && path_ == script.string() && ((missing && !L_) || (!missing && L_ && timestamp_ == stamp))) return;
        reset();
        project_ = ed.ws().project.dir;
        path_ = script.string();
        if (missing) return;
        timestamp_ = stamp;
        if (fs::file_size(script, ec) > 128 * 1024 || ec) { error_ = "Lua extension exceeds 128 KiB"; return; }
        std::ifstream f(script, std::ios::binary);
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        L_ = luaL_newstate();
        if (!L_) { error_ = "Cannot create Lua state"; return; }
        const luaL_Reg libs[] = {{"_G",luaopen_base},{LUA_STRLIBNAME,luaopen_string},{LUA_TABLIBNAME,luaopen_table},{LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8}};
        for (const auto& lib : libs) { luaL_requiref(L_, lib.name, lib.func, 1); lua_pop(L_, 1); }
        for (const char* global : {"dofile", "loadfile", "load", "collectgarbage"}) { lua_pushnil(L_); lua_setglobal(L_, global); }
        if (luaL_loadbufferx(L_, src.data(), src.size(), script.string().c_str(), "t") != LUA_OK) {
            error_ = lua_tostring(L_, -1); lua_pop(L_, 1); return;
        }
        invoke(0, 0);
    }
    static std::string field(lua_State* L, int table, const char* key, const char* fallback = "") {
        table = lua_absindex(L, table); lua_getfield(L, table, key);
        const char* s = lua_tostring(L, -1); std::string out = s ? s : fallback; lua_pop(L, 1); return out;
    }
    std::string field(int table, const char* key, const char* fallback = "") { return field(L_, table, key, fallback); }
    double num(int table, const char* key, double fallback = 0) {
        table = lua_absindex(L_, table); lua_getfield(L_, table, key);
        double out = lua_isnumber(L_, -1) ? lua_tonumber(L_, -1) : fallback;
        lua_pop(L_, 1); return std::isfinite(out) ? out : fallback;
    }
    void pushCtx(Editor& ed) {
        lua_newtable(L_);
        lua_pushinteger(L_, int(ed.selection.kind)); lua_setfield(L_, -2, "selection_kind");
        lua_pushinteger(L_, ed.selection.index); lua_setfield(L_, -2, "selection_index");
        lua_pushinteger(L_, int(ed.selected().size())); lua_setfield(L_, -2, "selection_count");
        lua_pushinteger(L_, int(ed.doc().data.entities.size())); lua_setfield(L_, -2, "object_count");
        lua_pushnumber(L_, ed.time()); lua_setfield(L_, -2, "time");
        lua_pushboolean(L_, ed.playing()); lua_setfield(L_, -2, "playing");
    }
    bool project(Editor& ed, const Viewport& vp, int table, const char* kx, const char* ky, const char* kz, ImVec2& out) {
        const render::float3 world{float(num(table,kx)), float(num(table,ky)), float(num(table,kz))};
        render::float2 point;
        if (!ed.sceneCam.worldToScreen(world, vp.pointsW, vp.pointsH, point)) return false;
        out = ImVec2(vp.originX + point.x, vp.originY + point.y); return true;
    }
    void runAction(Editor& ed, const char* fn, const std::string& id, const char* newString = nullptr, double newNumber = 0, int valueType = 0) {
        if (!L_) return;
        lua_getglobal(L_, fn);
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }
        pushCtx(ed);
        lua_pushlstring(L_, id.data(), id.size());
        if (valueType == 1) lua_pushlstring(L_, newString, std::strlen(newString));
        else if (valueType == 2) lua_pushnumber(L_, newNumber);
        else if (valueType == 3) lua_pushboolean(L_, newNumber != 0);
        const int argc = valueType ? 3 : 2;
        if (!invoke(argc, 1)) return;
        if (lua_istable(L_, -1)) {
            const int t = lua_absindex(L_, -1);
            const std::string cmd = field(t, "command");
            if (cmd == "focus_selection") {
                float x, z;
                if (ed.selectionPos(ed.selection, x, z)) ed.focusScene(x, z, 18);
            } else if (cmd == "clear_selection") ed.select({});
            else if (cmd == "select_entity") {
                const int index = int(num(t, "index", -1));
                if (index >= 0 && index < int(ed.doc().data.entities.size())) ed.selectEntity(index);
            } else if (cmd == "open_window") {
                const std::string window = field(t, "id");
                if (window.size() <= 64) ed.focusWindow(window);
            } else if (cmd == "status") {
                const std::string text = field(t, "text");
                if (text.size() <= 512) ed.status = text;
            }
        }
        lua_pop(L_, 1);
    }
    void drawWidget(Editor& ed, int t, int sequence, int depth = 0) {
        if (depth > 8) return;
        const std::string kind = field(t,"kind"), id = field(t,"id", ("item" + std::to_string(sequence)).c_str()), label = field(t,"label",id.c_str());
        ImGui::PushID(id.c_str());
        if (kind == "label") ImGui::TextWrapped("%s", label.c_str());
        else if (kind == "separator") ImGui::SeparatorText(label.c_str());
        else if (kind == "help") ImGui::TextDisabled("%s", label.c_str());
        else if (kind == "group" || kind == "foldout" || kind == "row") {
            const bool show = kind == "row" || ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (show) {
                lua_getfield(L_, t, "children");
                if (lua_istable(L_, -1)) {
                    const int children = lua_absindex(L_, -1);
                    const int n = std::min<int>(int(lua_rawlen(L_, children)), 128);
                    if (kind != "row") ImGui::Indent();
                    for (int i = 1; i <= n; ++i) {
                        lua_rawgeti(L_, children, i);
                        if (lua_istable(L_, -1)) {
                            if (kind == "row" && i > 1) ImGui::SameLine();
                            drawWidget(ed, lua_gettop(L_), i, depth + 1);
                        }
                        lua_pop(L_, 1);
                    }
                    if (kind != "row") ImGui::Unindent();
                }
                lua_pop(L_, 1);
            }
        }
        else if (kind == "button") { if (ImGui::Button(label.c_str())) runAction(ed,"on_action",id); }
        else if (kind == "checkbox") {
            if (!boolState_.count(id)) boolState_[id] = num(t,"value") != 0;
            bool& v = boolState_[id];
            if (ImGui::Checkbox(label.c_str(), &v)) runAction(ed,"on_change",id,nullptr,v?1:0,3);
        } else if (kind == "slider" || kind == "number") {
            if (!numberState_.count(id)) numberState_[id] = num(t,"value");
            double& v = numberState_[id];
            if (kind == "slider") {
                float f = float(v);
                if (ImGui::SliderFloat(label.c_str(), &f, float(num(t,"min")), float(num(t,"max",1)))) { v=f; runAction(ed,"on_change",id,nullptr,v,2); }
            } else {
                if (ImGui::InputDouble(label.c_str(), &v, .1, 1.0)) runAction(ed,"on_change",id,nullptr,v,2);
            }
        } else if (kind == "text") {
            if (!textState_.count(id)) textState_[id] = field(t,"value");
            std::string& v = textState_[id];
            if (ui::inputString(label.c_str(), v)) runAction(ed,"on_change",id,v.c_str(),0,1);
        }
        // Lua-generated widgets also accept editor context menu extensions.
        if (StudioContextMenus::get().available(ed) &&
            ImGui::BeginPopupContextItem("##extension_widget_menu")) {
            StudioContextMenus::get().draw(ed, "lua.widget", id.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    void createExample(Editor& ed) {
        namespace fs = std::filesystem;
        fs::path target = fs::path(ed.ws().project.dir) / "editor" / "extensions.lua";
        std::error_code ec;
        if (fs::exists(target,ec)) { ed.status = "editor/extensions.lua уже существует"; return; }
        fs::create_directories(target.parent_path(),ec);
        if (ec) { error_ = ec.message(); return; }
        std::ofstream f(target);
        f << "-- AIVERSE project-local editor extension\n"
             "function window(ctx)\n"
             "  return {\n"
             "    {kind='label', label='Objects: '..ctx.object_count},\n"
             "    {kind='button', id='focus', label='Focus selected object'},\n"
             "    {kind='button', id='inspector', label='Open Inspector'}\n"
             "  }\n"
             "end\n"
             "function on_action(ctx, id)\n"
             "  if id == 'focus' then return {command='focus_selection'} end\n"
             "  if id == 'inspector' then return {command='open_window',id='inspector'} end\n"
             "end\n"
             "function scene_gizmos(ctx)\n"
             "  return {{kind='circle',x=0,y=0,z=0,radius=10,r=0.3,g=0.8,b=1.0}}\n"
             "end\n";
        f.close(); reset(); load(ed);
    }
};

} // namespace aiverse::editor
