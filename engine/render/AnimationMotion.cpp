#include "AnimationMotion.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("motion matching: "+s);}
float num(const Json& j,float d){if(j.isNull())return d;double n=j.toDouble();if(!j.isNumber()||!std::isfinite(n)||std::abs(n)>1000000)fail("invalid number");return float(n);}
float3 pos(const mat4& m){return {m.at(0,3),m.at(1,3),m.at(2,3)};}
void append(std::vector<float>& v,float3 p){v.push_back(p.x);v.push_back(p.y);v.push_back(p.z);}
// Per-thread scratch: databases are shared immutable data used from MotionWeave workers.
struct Scratch {Pose pose;std::vector<mat4> a,b;};
Scratch& scratch(){thread_local Scratch s;return s;}
void sampleWorld(const Model& m,int clip,float t,Pose& p,std::vector<mat4>& w){bindPose(m,p);sampleClip(m.clips[size_t(clip)],std::clamp(t,0.f,m.clips[size_t(clip)].duration),p);worldMatrices(m,p,w);}
}
std::shared_ptr<const MotionDatabase> MotionDatabase::compile(const Model& m,const Json& j,const AnimationParameters& p){
  if (!j.isObject())
    fail("database must be object");
  auto d = std::make_shared<MotionDatabase>();
  d->model = &m;
  d->root = m.findNode(j.getStr("root"));
  if (d->root < 0)
    fail("unknown root bone");
  d->fps = num(j["fps"], 30);
  d->interval = num(j["searchInterval"], .1f);
  d->blend = num(j["blend"], .15f);
  d->continuationBias = num(j["continuationBias"], .05f);
  d->stripRoot = j.getBool("stripRoot", true);
  if (d->fps < 1 || d->fps > 120 || d->interval < .016f || d->interval > 10 || d->blend < 0 || d->blend > 10 ||
      d->continuationBias < 0)
    fail("invalid sampling/search settings");
  auto budget = j.getInt("maxCandidates", 8192);
  if (budget < 1 || budget > 8192)
    fail("maxCandidates must be 1..8192");
  d->maxCandidates = size_t(budget);
  if ((!j["bones"].isArray() && !(j["bones"].isObject() && j["bones"].size() == 0)) || j["bones"].size() > 8)
    fail("bones must have at most 8 entries");
  float pw = num(j["poseWeight"], 1), vw = num(j["velocityWeight"], .1f), tw = num(j["trajectoryWeight"], 2),
        fw = num(j["facingWeight"], 1);
  if (pw < 0 || vw < 0 || tw <= 0 || fw < 0)
    fail("invalid feature weights");
  if (j.has("facingAxis")) {
    const Json& a = j["facingAxis"];
    if (!a.isArray() || a.size() != 3) fail("facingAxis must be [x,y,z]");
    d->facingAxis = {num(a[0], 0), num(a[1], 0), num(a[2], 1)};
    if (length(d->facingAxis) < 1e-6f) fail("facingAxis is zero");
    d->facingAxis = normalize(d->facingAxis);
  }
  for (const auto &b : j["bones"].items()) {
    int id = m.findNode(b.str());
    if (id < 0)
      fail("unknown feature bone");
    d->bones.push_back(id);
    for (int k = 0; k < 3; ++k)
      d->weights.push_back(pw);
    for (int k = 0; k < 3; ++k)
      d->weights.push_back(vw);}
    if(!j["trajectory"].isArray()||j["trajectory"].size()==0||j["trajectory"].size()>4)fail("trajectory must have 1..4 samples");
    for(const auto& t:j["trajectory"].items()){
        Trajectory sample{num(t["time"],.3f),t.getStr("parameter"),t.getStr("facing")};
        if(sample.time<=0||sample.time>2||!p.vectors.count(sample.parameter))fail("invalid trajectory control/time");
        if(t.has("facing")&&(!t["facing"].isString()||!p.vectors.count(sample.facing)))fail("invalid trajectory facing control");
        for(int k=0;k<3;++k)d->weights.push_back(tw);
        if(!sample.facing.empty())for(int k=0;k<3;++k)d->weights.push_back(fw);
        d->trajectory.push_back(sample);
    }
    for(const auto& clip:m.clips)for(const auto& c:clip.channels)if(c.path==AnimChannel::Weights)d->morphFree=false;
    d->clipLoops.assign(m.clips.size(),1);
    if(!j["clips"].isArray()||j["clips"].size()==0||j["clips"].size()>64)fail("clips must contain 1..64 names");
    // Entries are clip names (looping) or {"clip":name,"loop":false} for
    // one-shot clips (stop, turn): they clamp at the end instead of wrapping.
    for(const auto& c:j["clips"].items()){
        const std::string name=c.isObject()?c.getStr("clip"):c.str();
        int clip=m.findClip(name);if(clip<0)fail("unknown clip '"+name+"'");float duration=m.clips[size_t(clip)].duration;
        if(c.isObject())d->clipLoops[size_t(clip)]=c.getBool("loop",true)?1:0;
        if(!std::isfinite(duration)||duration<=0)fail("clip duration must be positive");
        double count=std::ceil(double(duration)*d->fps);if(count>8192||d->frames.size()+size_t(count)>8192)fail("database exceeds 8192 frames; reduce fps/clips");
        for(size_t i=0;i<size_t(count);++i){Frame f{clip,float(i)/d->fps,{}};d->frames.push_back(std::move(f));}
    }
    for(auto& f:d->frames)d->features(f.clip,f.time,f.features);
    const size_t dims=d->weights.size();
    d->scale.resize(dims);for(size_t k=0;k<dims;++k)d->scale[k]=std::sqrt(d->weights[k]);
    d->scaled.resize(d->frames.size()*dims);
    for(size_t i=0;i<d->frames.size();++i)for(size_t k=0;k<dims;++k)d->scaled[i*dims+k]=d->frames[i].features[k]*d->scale[k];
    return d;
}
void MotionDatabase::poseFeatures(int clip,float t,std::vector<float>& out)const{
    out.clear();out.reserve(weights.size());
    if(bones.empty())return;
    auto& s=scratch();
    const float step=1/fps,duration=model->clips[size_t(clip)].duration;
    const float t0=std::clamp(t,0.f,std::max(0.f,duration-step)),t1=std::min(duration,t0+step);
    // Velocity is a forward difference over [t0,t1]; t0==t except at the clip end.
    sampleWorld(*model,clip,t,s.pose,s.a);
    const mat4 origin=s.a[size_t(root)].inverse();
    std::vector<float3> before(bones.size());
    if(t0!=t){std::vector<mat4> prev;sampleWorld(*model,clip,t0,s.pose,prev);for(size_t i=0;i<bones.size();++i)before[i]=pos(prev[size_t(bones[i])]);}
    else for(size_t i=0;i<bones.size();++i)before[i]=pos(s.a[size_t(bones[i])]);
    sampleWorld(*model,clip,t1,s.pose,s.b);
    const float inv=1/std::max(1e-6f,t1-t0);
    for(size_t i=0;i<bones.size();++i){const int bone=bones[i];append(out,origin.transformPoint(pos(s.a[size_t(bone)])));append(out,origin.transformDir((pos(s.b[size_t(bone)])-before[i])*inv));}
}
void MotionDatabase::features(int clip,float t,std::vector<float>& out)const{
    poseFeatures(clip,t,out);
    Pose p;std::vector<mat4> w;
    sampleWorld(*model,clip,t,p,w);
    const mat4 origin=w[size_t(root)].inverse();
    const float duration=model->clips[size_t(clip)].duration;
    const bool loop=loops(clip);
    auto rootAt=[&](float time){sampleWorld(*model,clip,time,p,w);return w[size_t(root)];};
    for(const auto& tr:trajectory){
        const float future=t+tr.time;
        mat4 target;
        if(!loop)target=rootAt(std::min(future,duration)); // one-shot: hold the final root
        else{
            // Loop extrapolation: root(t+nT) = delta^n * root(t), delta = root(T)*root(0)^-1.
            const float loops=std::floor(future/duration);
            target=rootAt(future-loops*duration);
            if(loops>0){const mat4 delta=rootAt(duration)*rootAt(0).inverse();for(int k=0;k<int(loops)&&k<256;++k)target=delta*target;}
        }
        append(out,origin.transformPoint(pos(target)));
        if(!tr.facing.empty()){
            float3 f=origin.transformDir(target.transformDir(facingAxis));f.y=0;const float l=length(f);
            append(out,l>1e-6f?f*(1/l):facingAxis);
        }
    }
}
size_t MotionDatabase::search(std::span<const float> query,int continuing,float time,float& cost)const{
    cost=std::numeric_limits<float>::infinity();size_t best=0;const size_t dims=weights.size();
    if(query.size()!=dims||frames.empty()||scaled.size()!=frames.size()*dims)return best;
    for(float v:query)if(!std::isfinite(v))return best;
    float q[64];std::vector<float> large;float* qs=q;
    if(dims>64){large.resize(dims);qs=large.data();}
    for(size_t j=0;j<dims;++j)qs[j]=query[j]*scale[j];
    const size_t budget=std::min(maxCandidates,frames.size());
    const float near=2/fps;
    for(size_t k=0;k<budget;++k){
        const size_t i=budget==frames.size()?k:k*frames.size()/budget;
        const auto& f=frames[i];
        // Every term is non-negative: start from the continuation bias and
        // stop as soon as the partial sum cannot beat the best candidate.
        float c=(f.clip!=continuing||std::abs(f.time-time)>near)?continuationBias:0.f;
        const float* row=scaled.data()+i*dims;
        size_t j=0;
        for(;j+4<=dims&&c<cost;j+=4){
            const float d0=qs[j]-row[j],d1=qs[j+1]-row[j+1],d2=qs[j+2]-row[j+2],d3=qs[j+3]-row[j+3];
            c+=d0*d0+d1*d1+d2*d2+d3*d3;
        }
        for(;j<dims&&c<cost;++j){const float d=qs[j]-row[j];c+=d*d;}
        if(c<cost){cost=c;best=i;}
    }
    return best;
}
void MotionPlayer::reset(std::shared_ptr<const MotionDatabase> db){db_=std::move(db);clip_=-1;time_=untilSearch_=fade_=cost_=0;source_.clear();query_.clear();}
void MotionPlayer::update(float dt,const AnimationParameters& parameters){
  if (!db_ || !std::isfinite(dt) || dt < 0)
    return;
  dt = std::min(dt, 1.f);
  fade_ += dt;
  untilSearch_ -= dt;
  if (clip_ >= 0) {
    const float duration = db_->model->clips[size_t(clip_)].duration;
    if (db_->loops(clip_))
      time_ = std::fmod(time_ + dt, duration);
    else {
      // One-shot clips hold their last frame and search immediately on arrival.
      const bool arriving = time_ < duration;
      time_ = std::min(time_ + dt, duration);
      if (arriving && time_ >= duration) untilSearch_ = 0;
    }
  }
  if (clip_ < 0 || untilSearch_ <= 0) {
    int featureClip = clip_ < 0 ? db_->frames[0].clip : clip_;
    db_->poseFeatures(featureClip, time_, query_);
    query_.resize(db_->weights.size());
    size_t at = db_->bones.size() * 6;
    for (const auto &tr : db_->trajectory) {
      auto it = parameters.vectors.find(tr.parameter);
      float3 v = it == parameters.vectors.end() ? float3{} : it->second;
      query_[at++] = v.x;
      query_[at++] = v.y;
      query_[at++] = v.z;
      if (!tr.facing.empty()) {
        auto f = parameters.vectors.find(tr.facing);
        float3 d = f == parameters.vectors.end() ? db_->facingAxis : f->second;
        d.y = 0;
        const float l = length(d);
        d = l > 1e-6f && std::isfinite(l) ? d * (1 / l) : db_->facingAxis;
        query_[at++] = d.x;
        query_[at++] = d.y;
        query_[at++] = d.z;
      }
    }
    size_t selected = db_->search(query_, clip_, time_, cost_);
    const auto &f = db_->frames[selected];
    if (std::isfinite(cost_) && (clip_ < 0 || f.clip != clip_ || std::abs(f.time - time_) > 2 / db_->fps)) {
      Pose frozen;  // crossfade from the pose shown this frame (may itself be mid-fade)
      evaluate(frozen);
      source_ = std::move(frozen);
      clip_ = f.clip;
      time_ = f.time;
      fade_ = 0;
    }
    untilSearch_ = db_->interval;
    }
}
void MotionPlayer::evaluate(Pose& out)const{
    if(!db_){out.clear();return;}bindPose(*db_->model,out);
    if(clip_>=0)sampleClip(db_->model->clips[size_t(clip_)],time_,out);
    if(db_->stripRoot){const auto& n=db_->model->nodes[size_t(db_->root)];out[size_t(db_->root)].t=n.t;out[size_t(db_->root)].r=n.r;}
    if(source_.size()==out.size()&&db_->blend>0&&fade_<db_->blend){float w=std::clamp(fade_/db_->blend,0.f,1.f);for(size_t i=0;i<out.size();++i){out[i].t=lerp(source_[i].t,out[i].t,w);out[i].s=lerp(source_[i].s,out[i].s,w);out[i].r=nlerp(source_[i].r,out[i].r,w);if(!db_->morphFree)for(int k=0;k<kMaxMorphTargets;++k)out[i].w[k]=source_[i].w[k]+(out[i].w[k]-source_[i].w[k])*w;}}
}
}
