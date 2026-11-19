// UI-independent editor API for native HTML5 hosts; intentionally no ImGui.
#pragma once
#include <string>
#include "apps/editor/core/Workspace.h"
namespace aiverse::studio {
class StudioSession {
public:
    explicit StudioSession(const std::string& engineRoot, const std::string& project = {});
    Json dispatch(const Json& request);
    const editor::Workspace& workspace() const { return ws_; }
    int selectedEntityIndex() const;
private:
    Json snapshot() const;
    int indexFor(const std::string& id) const;
    editor::Workspace ws_;
    std::string selected_;
};
}
