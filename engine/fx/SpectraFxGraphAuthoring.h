// SpectraFX authoring model: topology edits for a future node editor.
// Every operation validates a complete temporary graph before committing.
// Pure editor/data operations; no gameplay or GPU ownership.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "SpectraFxGraph.h"

namespace aiverse::fx {

class SpectraFxGraphAuthoring {
public:
    explicit SpectraFxGraphAuthoring(SpectraFxGraph graph = {})
        : graph_(std::move(graph)) {}

    const SpectraFxGraph& graph() const { return graph_; }
    uint64_t revision() const { return revision_; }

    // Graphs are stored topologically: one Input at index zero and later
    // nodes reference only earlier nodes. Retopologizing is explicit.
    bool replace(SpectraFxGraph next,std::string* error=nullptr) {
        if(!next.validate(error)) return false;
        graph_=std::move(next);
        ++revision_;
        return true;
    }

    bool append(SpectraFxNode node,std::string* error=nullptr) {
        auto nodes=graph_.nodes();
        if(nodes.empty()) {
            if(node.kind!=SpectraFxNodeKind::Input)
                return fail(error,"first SpectraFX node must be Input");
            nodes.push_back(std::move(node));
            // Input alone cannot validate until an Emit is appended.
            // Use createDefault() for valid initial graphs instead.
            return fail(error,"create a graph with Input and Emit before editing");
        }
        if(node.kind==SpectraFxNodeKind::Input)
            return fail(error,"cannot append a second Input");
        nodes.push_back(std::move(node));
        return commit(std::move(nodes),error);
    }

    // Change one node without modifying other nodes. The same validation
    // rejects self-references, forward links and invalid parameter values.
    bool update(size_t index,SpectraFxNode node,std::string* error=nullptr) {
        auto nodes=graph_.nodes();
        if(index>=nodes.size()) return fail(error,"node index out of range");
        if(index==0 && node.kind!=SpectraFxNodeKind::Input)
            return fail(error,"cannot replace graph Input");
        nodes[index]=std::move(node);
        return commit(std::move(nodes),error);
    }

    // Deleting a node that has downstream references is disallowed; the
    // editor must first reconnect them. Later source indices are shifted.
    bool remove(size_t index,std::string* error=nullptr) {
        auto nodes=graph_.nodes();
        if(index==0 || index>=nodes.size())
            return fail(error,"cannot remove Input or invalid node");
        for(size_t i=index+1;i<nodes.size();++i)
            if(nodes[i].source==index)
                return fail(error,"node has downstream connections");
        nodes.erase(nodes.begin()+static_cast<std::ptrdiff_t>(index));
        for(size_t i=index;i<nodes.size();++i)
            if(nodes[i].source>index) --nodes[i].source;
        return commit(std::move(nodes),error);
    }

    bool connect(size_t index,size_t source,std::string* error=nullptr) {
        auto nodes=graph_.nodes();
        if(index==0 || index>=nodes.size() || source>=index)
            return fail(error,"SpectraFX connection must reference earlier node");
        nodes[index].source=static_cast<uint32_t>(source);
        return commit(std::move(nodes),error);
    }

    static SpectraFxGraph createDefault(uint32_t cueId=0) {
        SpectraFxNode input;
        SpectraFxNode emit;
        emit.kind=SpectraFxNodeKind::Emit;
        emit.source=0;
        emit.cueId=cueId;
        SpectraFxGraph graph;
        graph.setNodes({input,emit});
        return graph;
    }

private:
    static bool fail(std::string* error,const char* reason) {
        if(error) *error=reason;
        return false;
    }
    bool commit(std::vector<SpectraFxNode> nodes,std::string* error) {
        SpectraFxGraph next;
        next.setNodes(std::move(nodes));
        return replace(std::move(next),error);
    }
    SpectraFxGraph graph_;
    uint64_t revision_=1;
};
} // namespace aiverse::fx
