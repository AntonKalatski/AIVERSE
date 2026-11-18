#include "engine/view/VisibilityField.h"
#include "modules/rts/view/RtsFog.h"
#include <type_traits>
#include <cassert>
int main() {
    static_assert(std::is_same_v<aiverse::view::VisibilityField,
                  aiverse::game::FogOfWar>);
    static_assert(std::is_same_v<aiverse::view::VisibilityLook,
                  aiverse::game::FogLook>);
    aiverse::view::VisibilityLook look;
    assert(look.show);
}
