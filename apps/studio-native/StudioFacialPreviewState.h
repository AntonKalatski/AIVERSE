// Project-scoped, owner-thread-only state for the HTML5 Studio Facial
// Control Rig preview. Browser never controls a filesystem path; StudioSession
// validates names, rig and model key before publishing a new revision.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/AnimationGraph.h"
#include <cstdint>
#include <map>
#include <string>

namespace aiverse::studio {
struct StudioFacialPreviewState final {
    bool active=false;
    std::string projectRoot;
    std::string modelKey;
    std::string rigName;
    Json authoredRig=Json::object();
    render::AnimationParameters controls;
    uint64_t generation=0; // model/rig replacement, NOT every slider change
    uint64_t controlsRevision=0;
};
} // namespace aiverse::studio
