// LENS camera presets: named starting points for the camera blocks (data only, no code paths per preset).
// A block picks one with "preset": "<name>"; its own keys go on top. Each preset is written in the keys of the
// block it belongs to (CameraBehavior.h): overhead = game.json "camera", follow = topDown.camera / a map's
// "camera", chase / firstPerson = the actor client blocks. Names describe the framing, not a game.
#pragma once
#include <string>
#include <vector>
#include "../data/Json.h"

namespace aiverse::render {

inline constexpr const char* kCameraPresetsJson = R"json({
  "overhead":           {"projection": "perspective", "pitch": 54.4, "fov": 43, "rotate": true},
  "overhead-flat":      {"projection": "perspective", "pitch": 70, "fov": 35, "rotate": false},
  "overhead-pixel":     {"projection": "ortho", "pitch": 90, "yaw": 0, "pixelsPerCell": 32, "rotate": false},
  "overhead-isometric": {"projection": "ortho", "pitch": 35.264, "yaw": 45, "rotate": false},

  "follow":             {"pitch": 58, "distance": 20, "minDistance": 10, "maxDistance": 40, "smoothing": 6,
                         "lookAhead": 2.5, "aimLookAhead": 1.5, "lookAheadSmoothing": 3, "bounds": true},
  "follow-overhead":    {"pitch": 90, "distance": 16, "minDistance": 8, "maxDistance": 30, "smoothing": 8,
                         "lookAhead": 2, "aimLookAhead": 1, "bounds": true},
  "follow-speed":       {"pitch": 90, "distance": 16, "minDistance": 8, "maxDistance": 30, "smoothing": 6,
                         "lookAhead": 1.5, "speedZoom": {"from": 4, "to": 18, "distance": 28, "smoothing": 1.5},
                         "velocityLookAhead": 0.35, "maxLookAhead": 8, "bounds": true},
  "follow-telephoto":   {"pitch": 57.8, "distance": 320, "minDistance": 116, "maxDistance": 420, "fov": 20,
                         "smoothing": 5, "lookAhead": 2.5, "fogFollow": true},

  "chase":              {"distance": 11, "height": 1.4, "pitch": 14.9, "lead": 2.5, "fov": 60.2, "near": 0.1, "far": 1500},
  "chase-low":          {"distance": 7, "height": 0.9, "pitch": 8, "lead": 1.5, "fov": 70, "near": 0.1, "far": 1500},

  "first-person":       {"fov": 69.9, "sensitivity": 0.0004, "aimSensitivity": 0.00015, "pitchLimit": 89.3, "near": 0.03, "far": 1000}
})json";

inline const Json& cameraPresets() {
    static const Json presets = Json::parse(kCameraPresetsJson);
    return presets;
}
inline std::vector<std::string> cameraPresetNames() {
    std::vector<std::string> out;
    for (const auto& [name, value] : cameraPresets().members()) out.push_back(name);
    return out;
}
// The block with its preset expanded: the preset's keys, then the block's own keys on top (nested objects such as
// "speedZoom" are replaced whole). Unknown preset: the block alone, error set.
inline Json resolveCameraPreset(const Json& block, std::string* error = nullptr) {
    if (!block.isObject() || !block["preset"].isString()) return block;
    const std::string name = block["preset"].str();
    Json out = Json::object();
    if (const Json& p = cameraPresets()[name]; p.isObject()) out = p;
    else if (error) *error = "camera: unknown preset '" + name + "'";
    for (const auto& [k, v] : block.members()) if (k != "preset") out.set(k, v);
    return out;
}
// Which camera body a preset belongs to (the block it is written for): overhead / follow / chase / firstPerson.
inline std::string cameraPresetBody(const std::string& name) {
    if (name.rfind("overhead", 0) == 0) return "overhead";
    if (name.rfind("follow", 0) == 0) return "follow";
    if (name.rfind("chase", 0) == 0) return "chase";
    if (name.rfind("first-person", 0) == 0) return "firstPerson";
    return "";
}

} // namespace aiverse::render
