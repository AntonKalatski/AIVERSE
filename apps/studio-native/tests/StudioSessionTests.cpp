#include "apps/studio-native/StudioSession.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace aiverse;
    aiverse::studio::StudioSession session(".");
    auto call = [&session](const char* json) { return session.dispatch(Json::parse(json)); };
    const Json snapshot = call(R"({"id":1,"method":"studio.snapshot","params":{}})");
    assert(snapshot["result"].isObject());
    assert(snapshot["result"]["scene"]["entities"].isArray());
    assert(snapshot["result"]["selectedId"].isNull());
    assert(call(R"({"id":2,"method":"studio.select","params":{"id":"entity:0"}})").has("error"));
    assert(call(R"({"id":3,"method":"workspace.call","params":{}})").has("error"));
    assert(call(R"({"id":4,"method":"studio.setProperty","params":{"entityId":"entity:0","key":"x","value":1}})").has("error"));
    assert(call(R"({"id":5,"method":"studio.select","params":{"id":null}})")["result"]["selectedId"].isNull());
    assert(call(R"({"id":6,"method":"studio.save","params":{}})").has("error"));
    assert(call(R"({"id":7,"method":"studio.undo","params":{}})").has("error"));
    assert(call(R"({"id":8,"method":"studio.redo","params":{}})").has("error"));
    assert(session.selectedEntityIndex() == -1);
    assert(call(R"({"id":21,"method":"studio.materials","params":{}})").has("error"));
    assert(call(R"({"id":22,"method":"studio.materialPatch","params":{"name":"steel","patch":{"metallic":0.5}}})").has("error"));
    assert(call(R"({"id":12,"method":"studio.lighting","params":{}})").has("error"));
    assert(call(R"({"id":13,"method":"studio.lightAdd","params":{}})").has("error"));
    assert(call(R"({"id":14,"method":"studio.lightUpdate","params":{"index":0,"key":"intensity","value":-5}})").has("error"));
    assert(call(R"({"id":15,"method":"studio.lightingSet","params":{"key":"storm","value":2}})").has("error"));
    // Asset listing must never expose a filesystem root without an open project.
    assert(call(R"({"id":9,"method":"studio.assets","params":{"dir":"../../"}})").has("error"));
    bool openRejected = false;
    try { session.openProject("/nonexistent/aiverse-project"); }
    catch (const std::exception&) { openRejected = true; }
    assert(openRejected);
    std::cout << "studio session contract PASS\n";
}
