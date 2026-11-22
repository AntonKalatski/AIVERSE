// The `prompt` Lua table: script choice menus (engine/game/Prompts.h, docs/prompts.md).
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "../../engine/game/Prompts.h"

namespace aiverse::game {

// ================================================================= Lua

namespace script {
namespace {

Prompts* promptsOf(Vm& vm) {
    Prompts* p = vm.module ? vm.module->prompts() : nullptr;
    return p && p->active() && vm.sim && p->store(*vm.sim) ? p : nullptr;
}
bool strings(lua_State* L, int idx, std::vector<std::string>& out) {
    if (lua_isnoneornil(L, idx)) return true;
    if (!lua_istable(L, idx)) return false;
    for (lua_Integer i = 1;; ++i) {
        lua_rawgeti(L, idx, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return true; }
        size_t n = 0;
        const char* s = luaL_tolstring(L, -1, &n);
        out.emplace_back(s, std::min<size_t>(n, 200));
        lua_pop(L, 2);
    }
}
// prompt.show(player, id, title, {lines}, {options}) -> shown
int pShow(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!p) return fail(vm, "the game has no prompts (rules \"prompts\": true)");
    if (!argInt(L, vm, 1, s, "player")) return -1;
    Prompt pr;
    pr.id = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "";
    pr.title = lua_type(L, 3) == LUA_TSTRING ? lua_tostring(L, 3) : "";
    if (!strings(L, 4, pr.lines) || !strings(L, 5, pr.options)) return fail(vm, "prompt.show: lines and options are lists of strings");
    lua_pushboolean(L, p->show(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)), std::move(pr)));
    return 1;
}
int pClose(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    if (p) p->close(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)));
    return 0;
}
// prompt.get(player) -> nil | {id, title, lines, options}
int pGet(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    const Prompt* pr = p ? p->of(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255))) : nullptr;
    if (!pr) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushstring(L, pr->id.c_str()); lua_setfield(L, -2, "id");
    lua_pushstring(L, pr->title.c_str()); lua_setfield(L, -2, "title");
    for (int k = 0; k < 2; ++k) {
        const auto& v = k ? pr->options : pr->lines;
        lua_newtable(L);
        for (size_t i = 0; i < v.size(); ++i) { lua_pushstring(L, v[i].c_str()); lua_rawseti(L, -2, lua_Integer(i + 1)); }
        lua_setfield(L, -2, k ? "options" : "lines");
    }
    return 1;
}

} // namespace

void openPromptApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {{"get", bindQ<pGet>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"show", bind<pShow>}, {"close", bind<pClose>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "prompt");
}

} // namespace script
} // namespace aiverse::game
