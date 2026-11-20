#include "apps/studio-native/StudioMaterialValidation.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace aiverse::studio;
int main(){
  assert(validStudioMaterialName("steel_1"));
  assert(!validStudioMaterialName("../bad")); assert(!validStudioMaterialName(""));
  assert(validStudioMaterialNumber("metallic",0));
  assert(validStudioMaterialNumber("roughness",1));
  assert(!validStudioMaterialNumber("metallic",1.001));
  assert(!validStudioMaterialNumber("metallic",std::numeric_limits<double>::infinity()));
  assert(!validStudioMaterialNumber("texture",.5));
  assert(validStudioMaterialNumber("normalScale",12));
  assert(!validStudioMaterialNumber("emissiveStrength",101));
  assert(validStudioMaterialBoolean("doubleSided")); assert(!validStudioMaterialBoolean("delete"));
  assert(validStudioMaterialText("alpha","blend"));
  assert(!validStudioMaterialText("alpha","transparent"));
  assert(validStudioMaterialText("team","tint"));
  assert(!validStudioMaterialText("base","../x"));
  std::cout<<"Studio material validation PASS\n";
}
