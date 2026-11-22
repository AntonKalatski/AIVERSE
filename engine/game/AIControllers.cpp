#include "AIControllers.h"
#include <mutex>

namespace aiverse::game {
namespace {
std::vector<AIControllerKind>& kinds() { static std::vector<AIControllerKind> k; return k; }
void ensureRegistered() { static std::once_flag once; std::call_once(once, [] { registerGameExtensions(); }); }
} // namespace

void registerAIController(AIControllerKind kind) {
    for (auto& k : kinds()) if (k.name == kind.name) { k = std::move(kind); return; }
    kinds().push_back(std::move(kind));
}

const AIControllerKind* findAIController(const std::string& name) {
    ensureRegistered();
    for (auto& k : kinds()) if (k.name == name) return &k;
    return nullptr;
}

std::vector<std::string> aiControllerNames() {
    ensureRegistered();
    std::vector<std::string> out{"builtin", "scripted"};
    for (auto& k : kinds()) out.push_back(k.name);
    return out;
}

} // namespace aiverse::game
