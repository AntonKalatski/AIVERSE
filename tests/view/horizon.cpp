#include "engine/view/Horizon.h"
#include <cassert>
using namespace aiverse;
int main(){
    const render::float3 eye{0,0,10};
    const float r=5;
    assert(!view::sphericalCapHiddenByHorizon(eye,r,{0,0,1},1.f,r));
    assert(view::sphericalCapHiddenByHorizon(eye,r,{0,0,-1},1.f,r));
    assert(!view::sphericalCapHiddenByHorizon(eye,r,{0,0,-1},-1.f,r));
    assert(!view::sphericalCapHiddenByHorizon({0,0,4},r,{0,0,-1},1.f,r));
}
