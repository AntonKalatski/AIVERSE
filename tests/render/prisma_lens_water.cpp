#include "engine/render/PrismaLensWater.h"
#include "engine/render/PrismaData.h"
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace aiverse::render;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    auto dry=advancePrismaLensWater({},false,.1f,4);
    check(dry.wetness==0,"dry camera must not invent lens water");
    auto immersed=advancePrismaLensWater({},true,.1f,4);
    check(immersed.wetness==1&&immersed.submerged,"immersion wets the lens");
    auto emerged=advancePrismaLensWater(immersed,false,.1f,4);
    check(!emerged.submerged&&emerged.wetness>.9f&&emerged.age>0,"exit retains draining droplets");
    auto shortDry=advancePrismaLensWater(immersed,false,.25f,.1f);
    check(shortDry.wetness==0,"authored dry time must clear lens");
    for(int i=0;i<50;++i)emerged=advancePrismaLensWater(emerged,false,.1f,4);
    check(emerged.wetness==0&&emerged.age==0,"lens dries completely without recurring rain");
    auto invalid=advancePrismaLensWater(immersed,false,std::numeric_limits<float>::quiet_NaN(),4);
    check(std::isfinite(invalid.age)&&invalid.wetness==1,"invalid delta must not poison history");
    const auto effect=parsePrismaEffect(aiverse::Json::parse(R"({"type":"lensWater","enabled":false,"dropSize":24,"dryTime":7})"),true);
    check(!effect.enabled&&effect.get("dropSize")==24&&effect.get("dryTime")==7,"lens water has independent settings and disable switch");
    const auto again=parsePrismaEffect(prismaEffectJson(effect),true);
    check(!again.enabled&&again.get("dryTime")==7,"lens effect round trips enabled state");
}
