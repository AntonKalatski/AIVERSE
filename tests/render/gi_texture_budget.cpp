#include "engine/render/GiTextureBudget.h"
#include "tests/AlwaysCheck.h"

using namespace aiverse::render;
static_assert(giFragmentTextureSlotsRequired(false,false,false)==14);
static_assert(giFragmentTextureSlotsRequired(true,false,false)==15);
static_assert(giFragmentTextureSlotsRequired(true,true,false)==16);
static_assert(giFragmentTextureSlotsRequired(true,true,true)==17);
static_assert(giFragmentTextureSlotsRequired(false,true,false)==0);
static_assert(giFragmentTextureSlotsRequired(true,false,true)==0);

int main() {
    // GLES3 minimum 16 fragment texture units: single/dual permitted,
    // Metal third atlas at texture(16) is NOT a GLES/WebGL2 permutation.
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,false,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,true,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(16,true,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(16,true,true,true));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(31,true,true,true));

    // Vulkan low descriptor caps must gracefully disable GI rather than
    // compiling a shader with unreachable texture units.
    for(int slots=0;slots<14;++slots)
        AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(slots,false,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(14,false,false,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(14,true,false,false));
    AIVERSE_TEST_CHECK(giFragmentTextureBudgetAllows(15,true,false,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(15,true,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(32,false,true,false));
    AIVERSE_TEST_CHECK(!giFragmentTextureBudgetAllows(32,true,false,true));
}
