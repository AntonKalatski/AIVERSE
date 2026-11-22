// Prompts: script-driven choice menus (see Prompts.h, docs/prompts.md), with the `prompt` Lua table.
#include "Prompts.h"
#include <algorithm>
#include "Systems.h"
#include "Systems.h"

namespace aiverse::game {

PromptStore* Prompts::store(Simulation& sim) const { return active() ? sim.world().resource<PromptStore>() : nullptr; }

void Prompts::setup(Simulation& sim, GameHost& m, const Json& rulesJson) {
    m_ = &m;
    enabled_ = rulesJson.getBool("prompts", false);
    if (!enabled_) return;
    sim.world().setResource<PromptStore>().players.assign(m.state(sim).players.size(), {});
}

const Prompt* Prompts::of(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    return st && player < st->players.size() && st->players[player].open() ? &st->players[player] : nullptr;
}

std::vector<PromptPick> Prompts::picks(Simulation& sim) const {
    std::vector<PromptPick> out;
    if (auto* st = store(sim))
        for (auto& k : st->picks) if (k.tick == sim.tick()) out.push_back(k);
    return out;
}

bool Prompts::show(Simulation& sim, uint8_t player, Prompt p) {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->players.size() || p.id.empty() || p.id.find(':') != std::string::npos) return false;
    if (p.options.empty() || p.options.size() > 9 || p.lines.size() > 8) return false;
    st->players[player] = std::move(p);
    return true;
}

void Prompts::close(Simulation& sim, uint8_t player) {
    auto* st = store(sim);
    if (st && player < st->players.size()) st->players[player] = {};
}

bool Prompts::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdPrompt) return false;
    auto* st = store(sim);
    if (!st || cmd.player == 0 || cmd.player >= st->players.size() || !m_->scripts()) return true;
    Prompt& p = st->players[cmd.player];
    st->picks.erase(std::remove_if(st->picks.begin(), st->picks.end(), [&](const PromptPick& k) { return k.tick != sim.tick(); }), st->picks.end());
    if (cmd.param == kPromptInteract) {
        if (p.open()) return true;
        st->picks.push_back({sim.tick(), cmd.player, std::string(), 0});
        m_->scripts()->allowNamed(sim, "on_interact", cmd.player, "");
        return true;
    }
    if (!p.open() || cmd.param < 0 || cmd.param >= int64_t(p.options.size())) return true;
    const std::string arg = p.id + ":" + std::to_string(cmd.param + 1);
    st->picks.push_back({sim.tick(), cmd.player, p.id, int32_t(cmd.param + 1)});
    p = {};   // closed before the script answers: it may open the next one
    m_->scripts()->allowNamed(sim, "on_prompt", cmd.player, arg);
    return true;
}

void Prompts::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    for (auto& p : st->players) {
        h.str(p.id); h.str(p.title);
        h.u32(uint32_t(p.lines.size())); for (auto& s : p.lines) h.str(s);
        h.u32(uint32_t(p.options.size())); for (auto& s : p.options) h.str(s);
    }
    for (auto& k : st->picks) { h.u32(k.tick); h.u32(k.player); h.str(k.id); h.i32(k.option); }
}

void Prompts::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->players.size());
    a(n);
    if (!a.writing() && n != st->players.size()) { a.fail(); return; }
    for (auto& p : st->players) a(p.id, p.title, p.lines, p.options);
    uint32_t np = uint32_t(st->picks.size());
    a(np);
    if (!a.writing()) { if (np > 100000) { a.fail(); return; } st->picks.assign(np, {}); }
    for (auto& k : st->picks) a(k.tick, k.player, k.id, k.option);
}

} // namespace aiverse::game
