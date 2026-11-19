// Stage 108 deferred: bounded event ring, overflow accounting and weak
// observer lifetime. A ring is NOT evidence of hardware GPU completion.
#include "engine/render/RetirementDiagnosticRing.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include <cassert>
#include <memory>
#include <vector>
using namespace aiverse::render;
int main() {
    auto ring=std::make_shared<RetirementDiagnosticRing>(2);
    auto observer=RetirementDiagnosticRing::observe(ring);
    observer({RetirementDiagnosticKind::Enqueued,0,1});
    observer({RetirementDiagnosticKind::TokenSubmitted,11,1});
    observer({RetirementDiagnosticKind::Pending,11,1});
    assert(ring->receivedCount()==3);
    assert(ring->overflowCount()==1);
    const auto snapshot=ring->snapshot();
    assert(snapshot.size()==2);
    assert(snapshot.front().kind==RetirementDiagnosticKind::TokenSubmitted);
    assert(snapshot.back().kind==RetirementDiagnosticKind::Pending);
    auto empty=std::make_shared<RetirementDiagnosticRing>(0);
    RetirementDiagnosticRing::observe(empty)(
        {RetirementDiagnosticKind::Failed,12,1});
    assert(empty->size()==0 && empty->overflowCount()==1);
    ring.reset();
    observer({RetirementDiagnosticKind::Completed,11,1}); // expired weak ref

    AsyncResourceRetirementQueue<std::unique_ptr<int>> queue;
    queue.setDiagnosticObserver([](const auto&) {
        throw 1; // diagnostic failure MUST NOT affect resource safety
    });
    queue.enqueue(std::make_unique<int>(1));
    assert(queue.pending()==1);
}
