// AIVERSE LENS: presentation-only camera rigs, lens and rail sampling.
// Reads one ECS snapshot AFTER SceneHierarchy update. Never mutates World.
#pragma once
#include "EcsCameraRender.h"
#include "../sim/CameraRigComponents.h"
#include "CameraRailPath.h"
#include "CameraBehavior.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace aiverse::render {
inline quat cameraLookRotation(float3 from,float3 to) {
    const float3 d=to-from;
    const float planar=std::sqrt(d.x*d.x+d.z*d.z);
    if (planar < 1.e-6f && std::abs(d.y) < 1.e-6f) return {};
    // AIVERSE local camera basis is -Z forward / +Y up.
    const float yaw=std::atan2(-d.x,-d.z);
    const float pitch=std::atan2(d.y,planar);
    return qnormalize(quat::axisAngle({0,1,0},yaw)*
                      quat::axisAngle({1,0,0},pitch));
}
inline float3 cameraEcsPos(Vec3 value,Vec3 origin={}) {
    return {float((value.x-origin.x).toDouble()),
            float((value.y-origin.y).toDouble()),
            float((value.z-origin.z).toDouble())};
}
inline float3 cameraRailPosition(const CameraRailComponent& rail,double seconds) {
    if(!cameraRailIsLegacy(rail))return cameraRailSample(rail,seconds).position;
    const int count=std::min<int>(rail.count,int(rail.points.size()));
    if (count<=0) return {};
    if (count==1) return cameraEcsPos(rail.points[0]);
    const double duration=std::max(0.001,rail.duration.toDouble());
    double t=seconds/duration;
    if (rail.loop) t=t-std::floor(t);
    else t=std::clamp(t,0.,1.);
    const double span=t*double(count-1);
    const int index=std::min(int(span),count-2);
    const float frac=float(span-index);
    return lerp(cameraEcsPos(rail.points[size_t(index)]),
                cameraEcsPos(rail.points[size_t(index+1)]),frac);
}
inline quat cameraEcsRotation(const Rotation& r) {
    return qnormalize(quat{float(r.x.toDouble()),float(r.y.toDouble()),float(r.z.toDouble()),float(r.w.toDouble())});
}
inline CameraFollowSettings cameraFollowSettings(const CameraFollowComponent& f) {
    auto v=[](Fixed x){return float(x.toDouble());};
    CameraFollowSettings c;
    c.pitch=v(f.pitch);c.yaw=v(f.yaw);c.distance=v(f.distance);
    c.minDistance=v(f.minDistance);c.maxDistance=v(f.maxDistance);
    c.smoothing=v(f.smoothing);c.lookAheadSmoothing=v(f.lookAheadSmoothing);
    c.lookAhead=0;c.aimLookAhead=0;                       // no stick on an ECS rig: the velocity lead only
    c.velocityLookAhead=v(f.velocityLookAhead);c.maxLookAhead=v(f.maxLookAhead);
    c.speedFrom=v(f.speedFrom);c.speedTo=v(f.speedTo);c.speedDistance=v(f.speedDistance);
    c.speedSmoothing=v(f.speedSmoothing);c.fov=v(f.fov);
    c.bounds=f.bounds;c.margin=v(f.margin);
    finishCameraFollow(c);
    return c;
}
inline bool validCameraLens(const CameraLensComponent& lens) {
    return lens.sensorHeightMm>0_fx && lens.focalLengthMm>0_fx &&
           lens.fNumber>0_fx && lens.focusDistance>0_fx &&
           lens.shutterSeconds>0_fx && lens.iso>0_fx;
}
// Optional transient lens output for a per-camera postprocess/exposure host.
struct CameraPhysicalLens {
    bool enabled=false;
    float focalMm=50.f, sensorHeightMm=24.f;
    float fNumber=2.8f, focusDistance=10.f, shutterSeconds=1.f/60, iso=100.f;
};
inline CameraPhysicalLens cameraPhysicalLens(const CameraLensComponent& l) {
    if (!validCameraLens(l)) return {};
    return {l.physicalFov,float(l.focalLengthMm.toDouble()),
        float(l.sensorHeightMm.toDouble()),float(l.fNumber.toDouble()),
        float(l.focusDistance.toDouble()),float(l.shutterSeconds.toDouble()),
        float(l.iso.toDouble())};
}
class CameraRigEvaluator {
    struct State { float3 pos{}; quat rotation{}; bool initialized=false; CameraFollow follow; Camera followCam; };
    std::unordered_map<uint64_t,State> state_;
    const World* source_=nullptr;
public:
    void reset(){source_=nullptr;state_.clear();}
    void forget(Entity e){state_.erase(e.packed());}

    // Returns true when the camera is valid. If a referenced rig target is
    // missing, leaves the authored camera pose untouched rather than aiming
    // at a fabricated origin. Uses dt only for smoothing; absolute seconds
    // drives rails/shake and permits cinematic seeking.
    bool apply(const World& world,EcsCameraView& view,double seconds,
               float dt,Vec3 renderOrigin={}) {
        if (!world.alive(view.entity) || !std::isfinite(seconds) ||
            !std::isfinite(dt) || dt<0) return false;
        if (source_ && source_!=&world) state_.clear();
        source_=&world;
        auto* rig=world.tryGetConst<CameraRigComponent>(view.entity);
        float3 desired=view.camera.eye();
        quat orientation=view.camera.usePose?view.camera.rotation:
            cameraLookRotation(desired,view.camera.target);
        bool followBody=false;   // the follow body smooths itself
        if (rig && rig->enabled && rig->mode!=CameraRigMode::Static) {
            float3 target{};
            const bool needsTarget=rig->mode==CameraRigMode::Follow ||
                                   rig->mode==CameraRigMode::Orbit ||
                                   rig->mode==CameraRigMode::LookAt ||
                                   rig->mode==CameraRigMode::Chase ||
                                   rig->mode==CameraRigMode::FirstPerson;
            quat targetRotation{};
            if (needsTarget) {
                const auto* xf=world.tryGetConst<WorldTransform>(rig->target);
                if (!xf) return true; // no implicit target at world origin
                target=cameraEcsPos(xf->position,renderOrigin);
                targetRotation=cameraEcsRotation(xf->rotation);
            }
            if (rig->mode==CameraRigMode::Follow &&
                world.tryGetConst<CameraFollowComponent>(view.entity)) {
                // The shared follow body (the game follow camera): its own smoothing, lead, speed zoom, bounds.
                const auto& fc=*world.tryGetConst<CameraFollowComponent>(view.entity);
                auto& st=state_[view.entity.packed()];
                const CameraFollowSettings settings=cameraFollowSettings(fc);
                CameraFollowInput in;
                in.focus=target;in.moveLookAhead=false;in.focusHeight=true;
                in.dt=st.initialized?dt:0.f;
                if(fc.bounds) {
                    const float3 o=cameraEcsPos(renderOrigin);
                    in.mapX=float(fc.minX.toDouble())-o.x;in.mapZ=float(fc.minZ.toDouble())-o.z;
                    in.mapW=float((fc.maxX-fc.minX).toDouble());in.mapH=float((fc.maxZ-fc.minZ).toDouble());
                }
                if(!st.initialized){st.follow.reset();st.followCam=Camera{};st.followCam.distance=settings.distance;}
                st.follow.update(settings,in,st.followCam);
                desired=st.followCam.eye();
                orientation=cameraLookRotation(desired,st.followCam.target);
                if(settings.fov>0)view.camera.fovY=st.followCam.fovY;
                followBody=true;
            } else if (rig->mode==CameraRigMode::Follow) {
                desired=target+cameraEcsPos(rig->offset);
                if(rig->trackTarget) orientation=cameraLookRotation(desired,target);
            } else if(rig->mode==CameraRigMode::Orbit) {
                const float yaw=float(rig->yaw.toDouble());
                const float pitch=float(rig->pitch.toDouble());
                const float cp=std::cos(pitch);
                const float distance=std::max(.01f,float(rig->distance.toDouble()));
                desired=target+float3{std::sin(yaw)*cp,std::sin(pitch),std::cos(yaw)*cp}*distance;
                orientation=cameraLookRotation(desired,target);
            } else if(rig->mode==CameraRigMode::Chase) {
                constexpr float tau=6.28318530718f;
                const float3 f=targetRotation.rotate({0,0,1});
                const float heading=std::atan2(f.x,f.z)/tau+float(rig->yaw.toDouble())/tau;
                CameraChaseSettings chase;
                chase.distance=std::max(.01f,float(rig->distance.toDouble()));
                chase.pitch=float(rig->pitch.toDouble());
                chase.height=float(rig->offset.y.toDouble());
                chase.lead=float(rig->offset.z.toDouble());
                const Camera c=chaseCamera(target,heading,chase);
                desired=c.eye();
                orientation=cameraLookRotation(desired,c.target);
            } else if(rig->mode==CameraRigMode::FirstPerson) {
                constexpr float pi=3.14159265358979f;
                desired=target+targetRotation.rotate(cameraEcsPos(rig->offset));
                orientation=qnormalize(targetRotation*quat::axisAngle({0,1,0},pi+float(rig->yaw.toDouble()))*
                                       quat::axisAngle({1,0,0},float(rig->pitch.toDouble())));
            } else if(rig->mode==CameraRigMode::LookAt) {
                orientation=cameraLookRotation(desired,target);
            } else if(rig->mode==CameraRigMode::Rail) {
                if(const auto* rail=world.tryGetConst<CameraRailComponent>(view.entity);
                   rail && rail->count) {
                    desired=cameraRailPosition(*rail,seconds)-cameraEcsPos(renderOrigin);
                    if(rig->trackTarget && rig->target.valid()) {
                        if (const auto* xf=world.tryGetConst<WorldTransform>(rig->target))
                            orientation=cameraLookRotation(desired,cameraEcsPos(xf->position,renderOrigin));
                    } else if(cameraRailIsLegacy(*rail)) {
                        const float3 next=cameraRailPosition(*rail,seconds+.016);
                        if(length(next-(desired+cameraEcsPos(renderOrigin)))>1.e-4f)
                            orientation=cameraLookRotation(desired,next-cameraEcsPos(renderOrigin));
                    } else if(const auto sample=cameraRailSample(*rail,seconds);
                              length(sample.tangent)>0.5f) {
                        orientation=cameraLookRotation(desired,desired+sample.tangent);
                    }
                }
            }
        }
        auto& s=state_[view.entity.packed()];
        const float smooth=rig&&!followBody?float(std::max(0.,rig->smoothing.toDouble())):0.f;
        const float k=(!s.initialized || smooth<=0)?1.f:
                        1.f-std::exp(-smooth*dt);
        if(!s.initialized){s.pos=desired;s.rotation=orientation;s.initialized=true;}
        else {s.pos=lerp(s.pos,desired,k);s.rotation=slerp(s.rotation,orientation,k);}
        float3 position=s.pos;
        quat rotation=s.rotation;
        if (const auto* shake=world.tryGetConst<CameraShakeComponent>(view.entity)) {
            if (const auto offset=cameraShakeOffset(*shake,seconds); offset.active) {
                position+=offset.position;
                // Local yaw (Y), pitch (X), roll (Z); the sine style only rolls.
                rotation=qnormalize(rotation*quat::axisAngle({0,1,0},offset.rotation.y)*
                                    quat::axisAngle({1,0,0},offset.rotation.x)*
                                    quat::axisAngle({0,0,1},offset.rotation.z));
            }
        }
        view.camera.setPose(position,rotation);
        if (const auto* lens=world.tryGetConst<CameraLensComponent>(view.entity);
            lens && lens->physicalFov && validCameraLens(*lens)) {
            const double sensor=lens->sensorHeightMm.toDouble();
            const double focal=lens->focalLengthMm.toDouble();
            view.camera.fovY=float(2.0*std::atan(sensor/(2.0*focal)));
        }
        return true;
    }
};
} // namespace aiverse::render
