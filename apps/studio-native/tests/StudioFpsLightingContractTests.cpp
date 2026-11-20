#include "apps/studio-native/StudioFpsLightingContract.h"
#include <cassert>
#include <iostream>
using namespace aiverse::studio;
int main() {
    assert(validFpsLightingChange("studio.lightingSet","time",.5,true,false,{},false));
    assert(!validFpsLightingChange("studio.lightingSet","time",1.5,true,false,{},false));
    assert(!validFpsLightingChange("studio.lightingSet","name",.5,true,false,{},false));
    assert(validFpsLightingChange("studio.lightUpdate","name",0,false,true,"Sun",false));
    assert(validFpsLightingChange("studio.lightUpdate","nightOnly",0,false,false,{},true));
    assert(!validFpsLightingChange("studio.lightUpdate","deleteAll",0,false,false,{},true));
    assert(!validFpsLightingChange("someArbitraryOp","x",0,true,false,{},false));
    std::cout << "Studio FPS Lighting authorization PASS\n";
}
