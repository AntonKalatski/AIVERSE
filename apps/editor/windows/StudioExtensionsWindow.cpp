// Project-defined editor panels and Scene overlays, powered by Lua 5.4.
// The project script cannot directly mutate the simulation or invoke C++ callbacks.
#include "../Editor.h"
#include "StudioLuaExtensions.h"

namespace aiverse::editor {
class StudioExtensionsWindow final : public EditorWindow {
public:
    const char* id() const override { return "studio_extensions"; }
    const char* title() const override { return "Lua Editor"; }
    const char* group() const override { return "service"; }
    const char* dockArea() const override { return "right-bottom"; }
    bool openByDefault() const override { return false; }
    void draw(Editor& editor) override { StudioLuaExtensions::get().drawPanel(editor); }
};
AIVERSE_EDITOR_WINDOW(StudioExtensionsWindow);
} // namespace aiverse::editor
