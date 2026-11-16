// Which RHI backend the window starts with on Windows, Linux and Android (macOS / iOS: Metal, web: WebGL2).
//
//   game.json  "graphics": "vulkan"                                 // every platform that has it
//              "graphics": {"windows": "vulkan", "android": "gl"}   // per platform, others keep GL
//   command line  --gfx vulkan|gl        environment  AIVERSE_GFX=vulkan|gl   (wins over both)
//
// GL is the default. A Vulkan request falls back to GL when the machine has no Vulkan driver.
#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include "Platform.h"
#include "engine/data/Json.h"

namespace aiverse::platform {

inline bool graphicsNameValid(const std::string& s) { return s == "gl" || s == "vulkan"; }

// Problems of a "graphics" value; empty = fine. Russian, for the editor.
inline std::vector<std::string> graphicsSettingsProblems(const Json& g) {
    std::vector<std::string> out;
    if (g.isNull()) return out;
    auto check = [&](const Json& v, const std::string& where) {
        if (!v.isString() || !graphicsNameValid(v.str())) out.push_back(where + "graphics: нужно \"gl\" или \"vulkan\"");
    };
    if (g.isObject()) { for (auto& [k, v] : g.members()) check(v, k + ": "); }
    else check(g, "");
    return out;
}

// game.json "graphics" -> cfg.graphics for `platform`; keeps a value set before (command line).
inline void applyGraphicsSettings(AppConfig& cfg, const Json& g, const std::string& platform) {
    if (!cfg.graphics.empty()) return;
    const Json& v = g.isObject() ? g[platform] : g;
    if (v.isString() && graphicsNameValid(v.str())) cfg.graphics = v.str();
}

// The backend to try first: AIVERSE_GFX, then cfg.graphics.
inline bool wantVulkan(const AppConfig& cfg) {
    if (const char* e = std::getenv("AIVERSE_GFX"); e && e[0]) return std::string(e) == "vulkan";
    return cfg.graphics == "vulkan";
}

} // namespace aiverse::platform
