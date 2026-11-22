#include "apps/editor/TerraWeaveForestPreview.h"
#include "apps/editor/TerraWeavePlacementMask.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>

using namespace aiverse::editor;

int main() {
    TerraWeaveForestPreview preview;
    // Regression: the editor Horizon preview must fit the Horizon atlas
    // budget for any map size (64 px x two views used to throw and silently
    // disable the forest overlay as soon as horizon=true was requested).
    for (int side : {16, 192, 512, 2048})
        for (int32_t cell : {50, 100, 400}) {
            const auto hs = TerraWeaveForestPreview::editorHorizonSettings(side, side, cell);
            aiverse::terraweave::validateHorizonSettings(hs);
            assert(hs.orientationViews == 2 && hs.crownDetail == 3);
        }
    assert(!preview.enabled());
    assert(preview.generatedInstances() == 0);
    assert(preview.visibleInstances() == 0);
    preview.setEnabled(true);
    assert(preview.enabled());
    preview.setEnabled(false);
    assert(!preview.enabled());

    // Generated metres/cell may differ from the editor's fixed 1m/cell.
    assert(terraWeaveWorldFromEditorCm(200, 1000, 200) == 1400);
    assert(terraWeaveEditorFromWorldCm(1400, 1000, 200) == 200);
    assert(terraWeaveWorldFromEditorCm(-300, -1000, 150) == -1450);
    assert(terraWeaveEditorFromWorldCm(-1450, -1000, 150) == -300);
    bool badScale = false;
    try { (void)terraWeaveEditorFromWorldCm(0, 0, 0); }
    catch (const std::out_of_range&) { badScale = true; }
    assert(badScale);

    MapData map;
    map.w = 20;
    map.h = 20;
    map.cells.assign(20 * 20, '.');
    assert(terraWeavePreviewAllowsPlacement(map, 1200, 1500));

    map.cells[4 * 20 + 3] = '~';
    assert(!terraWeavePreviewAllowsPlacement(map, 320, 450));
    map.cells[4 * 20 + 3] = '#';
    assert(!terraWeavePreviewAllowsPlacement(map, 320, 450));
    map.cells[4 * 20 + 3] = '.';
    assert(terraWeavePreviewAllowsPlacement(map, 320, 450));

    DocEntity a;
    a.x = 5; a.z = 5;
    map.entities.push_back(a);
    assert(!terraWeavePreviewAllowsPlacement(map, 500, 500));
    assert(terraWeavePreviewAllowsPlacement(map, 1300, 1300));

    DocEntity b;
    b.x = 10; b.z = 3;
    map.entities.push_back(b);
    DocEntity c;
    c.x = 10; c.z = 16;
    map.entities.push_back(c);
    map.roads.emplace_back(1, 2);
    assert(!terraWeavePreviewAllowsPlacement(map, 1080, 900));
    assert(terraWeavePreviewAllowsPlacement(map, 1580, 900));

    assert(!terraWeavePreviewAllowsPlacement(map, -1, 500));
    assert(!terraWeavePreviewAllowsPlacement(map, 2200, 500));

    bool threw = false;
    try { (void)terraWeavePreviewAllowsPlacement(map, 10, 10, 0); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
