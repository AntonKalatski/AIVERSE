#include "apps/studio-native/StudioLightingValidation.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace aiverse::studio;
int main() {
    assert(validLightingScalar("time",0)); assert(validLightingScalar("time",1));
    assert(!validLightingScalar("time",1.000001)); assert(!validLightingScalar("storm",-0.01));
    assert(!validLightingScalar("arbitrary",0.5));
    assert(validLightingScalar("dayLength",86400));
    assert(!validLightingScalar("dayLength",100000001));
    assert(!validLightingScalar("clouds",std::numeric_limits<double>::quiet_NaN()));
    assert(validLightScalar("x",-1000000)); assert(!validLightScalar("z",1000001));
    assert(validLightScalar("radius",0.1)); assert(!validLightScalar("radius",0));
    assert(!validLightScalar("intensity",std::numeric_limits<double>::infinity()));
    assert(validLightText("name","My lamp"));
    assert(validLightText("bake","mixed")); assert(!validLightText("bake","unknown"));
    assert(validLightText("type","point")); assert(!validLightText("type","directional"));
    assert(validLightBoolean("nightOnly")); assert(!validLightBoolean("remove"));
    std::cout << "Studio lighting validation PASS\n";
}
