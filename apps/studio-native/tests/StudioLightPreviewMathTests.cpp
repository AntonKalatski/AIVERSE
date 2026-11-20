#include "apps/studio-native/StudioLightPreviewMath.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace aiverse::studio;
int main() {
    auto noon=studioPreviewDayLighting(.5,.15,0,0);
    auto night=studioPreviewDayLighting(0,.15,0,0);
    assert(noon.sun>night.sun && noon.ambient>night.ambient);
    assert(night.night>.99f && noon.night<.01f);
    auto covered=studioPreviewDayLighting(.5,.15,.9,0);
    assert(covered.sun<noon.sun && covered.clouds==.9f);
    auto storm=studioPreviewDayLighting(.5,.15,0,1);
    assert(storm.sun<noon.sun && storm.clouds>.8f);
    auto invalid=studioPreviewDayLighting(std::numeric_limits<double>::quiet_NaN(),-1,
                        std::numeric_limits<double>::infinity(),2);
    assert(std::isfinite(invalid.sun)&&std::isfinite(invalid.ambient));
    assert(invalid.ambient>=0 && invalid.ambient<=1);
    std::cout<<"Studio light preview math PASS\n";
}
