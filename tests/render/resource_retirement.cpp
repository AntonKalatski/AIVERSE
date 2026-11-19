// Stage 99: CPU contract for exact retirement fence and staging order.
// Registered for deferred native execution; no GPU Device is required here.
#include "engine/render/ResourceRetirementQueue.h"
#include <cassert>
#include <memory>
#include <vector>

int main() {
    aiverse::render::ResourceRetirementQueue<std::unique_ptr<int>> retired;
    retired.enqueue(std::make_unique<int>(11));
    retired.enqueue(std::make_unique<int>(22));
    assert(retired.pending()==2);
    bool idle=false;
    int fences=0;
    std::vector<int> destroyed;
    auto fence=[&]{++fences;return idle;};
    auto dispose=[&](const std::unique_ptr<int>& item) {
        destroyed.push_back(*item);
    };

    // Frame number, wall time and age of the queue do not release anything.
    assert(!retired.collect(fence,dispose));
    assert(retired.pending()==2 && destroyed.empty() && fences==1);
    assert(!retired.collect(fence,dispose));
    assert(destroyed.empty() && fences==2);

    idle=true;
    assert(retired.collect(fence,dispose));
    assert(retired.empty() && fences==3);
    assert((destroyed==std::vector<int>{11,22}));
    assert(retired.collect(fence,dispose));
    assert(fences==3); // no unnecessary GPU wait when nothing is pending

    retired.enqueue(std::make_unique<int>(33));
    idle=false;
    assert(!retired.collect(fence,dispose));
    assert(destroyed.size()==2 && retired.pending()==1);
    idle=true;
    assert(retired.collect(fence,dispose));
    assert((destroyed==std::vector<int>{11,22,33}));
}
