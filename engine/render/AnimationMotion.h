#pragma once
#include "AnimationGraph.h"
namespace aiverse::render {
class MotionDatabase {
public:
    struct Frame {int clip=0;float time=0;std::vector<float> features;};
    // Future root sample: position (always) and, when `facing` names a vector
    // parameter, the root's facing direction (xz, normalized) at that time.
    struct Trajectory {float time=.3f;std::string parameter,facing;};
    static std::shared_ptr<const MotionDatabase> compile(const Model&,const Json&,const AnimationParameters&);
    // Full feature vector (pose + trajectory) of a clip frame.
    void features(int clip,float time,std::vector<float>& out) const;
    // Pose part only (bone positions/velocities in root space): the prefix a
    // runtime query needs; the trajectory part comes from host parameters.
    void poseFeatures(int clip,float time,std::vector<float>& out) const;
    size_t search(std::span<const float> query,int continuationClip,float continuationTime,float& cost) const;
    bool loops(int clip) const {return clip<0||size_t(clip)>=clipLoops.size()||clipLoops[size_t(clip)]!=0;}
    const Model* model=nullptr;
    int root=0;
    float fps=30,interval=.1f,blend=.15f,continuationBias=.05f;
    size_t maxCandidates=8192;
    bool stripRoot=true;
    bool morphFree=true;          // no clip animates morph weights: blends skip them
    float3 facingAxis{0,0,1};     // root-local forward used by facing features
    std::vector<int> bones;
    std::vector<Trajectory> trajectory;
    std::vector<float> weights;
    std::vector<uint8_t> clipLoops; // per model clip; clips default to looping
    std::vector<Frame> frames;
    // Search layout: features * sqrt(weight), one contiguous row per frame.
    std::vector<float> scaled, scale;
};
class MotionPlayer {
public:
    void reset(std::shared_ptr<const MotionDatabase>);
    void update(float dt,const AnimationParameters&);
    void evaluate(Pose&) const;
    int clip() const {return clip_;} float time()const{return time_;} float cost()const{return cost_;}
private:
    std::shared_ptr<const MotionDatabase> db_;
    int clip_=-1;
    float time_=0,untilSearch_=0,fade_=0,cost_=0;
    Pose source_;
    std::vector<float> query_;
};
}
