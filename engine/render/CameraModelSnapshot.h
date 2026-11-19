// AIVERSE LENS — one-frame owned snapshot of finalized ModelDraw calls.
// Record after a single animation/View update. Secondary cameras replay
// models and attachments but NEVER advance animation or redispatch events.
#pragma once
#include "Renderer3D.h"
#include <cstddef>
#include <utility>
#include <vector>

namespace aiverse::render {
class CameraModelSnapshot {
    struct Entry {
        ModelDraw draw;
        std::vector<mat4> nodeWorld;
        std::vector<Material> materials;
        std::vector<int> nodeMaterial;
        std::vector<NodePose> pose;
        BakedSample baked{};
        bool hasNodeWorld=false,hasMaterials=false,hasNodeMaterial=false;
        bool hasPose=false,hasBaked=false;
    };
    std::vector<Entry> entries_;
    size_t dropped_=0,maximum_=100000;
public:
    explicit CameraModelSnapshot(size_t maxPackets=100000):maximum_(maxPackets){}
    void clear(){entries_.clear();dropped_=0;}
    size_t size()const{return entries_.size();}
    size_t dropped()const{return dropped_;}
    void reserve(size_t count){entries_.reserve(count);}
    void record(const ModelDraw& source) {
        if(!source.model)return;
        if(entries_.size()>=maximum_){++dropped_;return;}
        Entry e;
        e.draw=source;
        e.draw.staticCache=nullptr; // transient pointer invalid after main pass
        if(source.nodeWorld){e.nodeWorld=*source.nodeWorld;e.hasNodeWorld=true;}
        if(source.materials){e.materials=*source.materials;e.hasMaterials=true;}
        if(source.nodeMaterial){e.nodeMaterial=*source.nodeMaterial;e.hasNodeMaterial=true;}
        if(source.pose){e.pose=*source.pose;e.hasPose=true;}
        if(source.baked){e.baked=*source.baked;e.hasBaked=true;}
        // The next replay reconstructs pointers to this Entry's own storage.
        // No transient pointer from the caller escapes an active submission.
        e.draw.nodeWorld=nullptr;e.draw.materials=nullptr;
        e.draw.nodeMaterial=nullptr;e.draw.pose=nullptr;e.draw.baked=nullptr;
        entries_.push_back(std::move(e));
    }
    void beginCapture(Renderer3D& renderer) {
        clear();
        renderer.setModelPacketTap([this](const ModelDraw& draw){record(draw);});
    }
    void endCapture(Renderer3D& renderer) {
        renderer.setModelPacketTap({});
    }
    class CaptureGuard {
        CameraModelSnapshot* snapshot_=nullptr;
        Renderer3D* renderer_=nullptr;
    public:
        CaptureGuard()=default;
        CaptureGuard(CameraModelSnapshot& snapshot,Renderer3D& renderer) {
            begin(snapshot,renderer);
        }
        void begin(CameraModelSnapshot& snapshot,Renderer3D& renderer) {
            end();
            snapshot_=&snapshot;renderer_=&renderer;
            snapshot_->beginCapture(renderer);
        }
        CaptureGuard(const CaptureGuard&)=delete;
        CaptureGuard& operator=(const CaptureGuard&)=delete;
        ~CaptureGuard(){end();}
        void end() {
            if(snapshot_&&renderer_)snapshot_->endCapture(*renderer_);
            snapshot_=nullptr;renderer_=nullptr;
        }
    };
    CaptureGuard capture(Renderer3D& renderer) {
        return CaptureGuard(*this,renderer);
    }
    // The caller must first endCapture(), then beginScene() with each
    // secondary camera. Model/texture resources are borrowed from the host;
    // do not invalidate them until all camera submissions are complete.
    template<class Submit>
    void forEach(Submit&& submit)const {
        for(const auto& e:entries_) {
            ModelDraw d=e.draw;
            if(e.hasNodeWorld)d.nodeWorld=&e.nodeWorld;
            if(e.hasMaterials)d.materials=&e.materials;
            if(e.hasNodeMaterial)d.nodeMaterial=&e.nodeMaterial;
            if(e.hasPose)d.pose=&e.pose;
            if(e.hasBaked)d.baked=&e.baked;
            submit(d);
        }
    }
    void submit(Renderer3D& renderer)const {
        forEach([&](const ModelDraw& d){renderer.drawModel(d);});
    }
};
} // namespace aiverse::render
