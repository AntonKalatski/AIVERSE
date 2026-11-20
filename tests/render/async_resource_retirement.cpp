// Stage 100 deferred source regression: ordered nonblocking retirement.
// No real GPU backend required; production backend tests still pending.
#include "engine/render/AsyncResourceRetirementQueue.h"
#include <cassert>
#include <memory>
#include <set>
#include <vector>

int main() {
    using Queue=aiverse::render::AsyncResourceRetirementQueue<std::unique_ptr<int>>;
    Queue queue;
    uint64_t issued=0;
    bool allowToken=true,allowWait=false;
    unsigned syncWaits=0;
    std::set<uint64_t> complete;
    std::vector<uint64_t> closed;
    std::vector<int> destroyed;
    auto begin=[&]() -> uint64_t {
        return allowToken?++issued:0;
    };
    auto done=[&](uint64_t token) {return complete.contains(token);};
    auto close=[&](uint64_t token) {closed.push_back(token);};
    auto wait=[&]() {++syncWaits;return allowWait;};
    auto dispose=[&](std::unique_ptr<int>& model) {
        destroyed.push_back(*model);
    };

    queue.enqueue(std::make_unique<int>(10));
    assert(!queue.collect(begin,done,close,wait,dispose));
    assert(issued==1 && queue.pending()==1);
    assert(syncWaits==0 && destroyed.empty());

    // A later eviction belongs to a second token, not token #1.
    queue.enqueue(std::make_unique<int>(20));
    assert(!queue.collect(begin,done,close,wait,dispose));
    assert(issued==2 && queue.pending()==2 && destroyed.empty());

    complete.insert(1);
    assert(!queue.collect(begin,done,close,wait,dispose));
    assert((destroyed==std::vector<int>{10}));
    assert((closed==std::vector<uint64_t>{1}));
    assert(queue.pending()==1);
    complete.insert(2);
    assert(queue.collect(begin,done,close,wait,dispose));
    assert((destroyed==std::vector<int>{10,20}));
    assert(queue.empty() && syncWaits==0);

    // Fail-closed fallback: if async token creation fails, do not free.
    allowToken=false;
    queue.enqueue(std::make_unique<int>(30));
    assert(!queue.collect(begin,done,close,wait,dispose));
    assert(queue.pending()==1 && destroyed.size()==2);
    allowWait=true;
    assert(queue.collect(begin,done,close,wait,dispose));
    assert((destroyed==std::vector<int>{10,20,30}));

    // Shutdown explicitly drains pending in-flight tokens only after an
    // externally confirmed full GPU completion barrier.
    allowToken=true;allowWait=false;
    queue.enqueue(std::make_unique<int>(40));
    assert(!queue.collect(begin,done,close,wait,dispose));
    assert(!queue.drain(wait,close,dispose));
    assert(queue.pending()==1);
    allowWait=true;
    assert(queue.drain(wait,close,dispose));
    assert(queue.empty());
    assert((destroyed==std::vector<int>{10,20,30,40}));
    assert(closed.back()==3);
    assert(queue.drain(wait,close,dispose));
}
