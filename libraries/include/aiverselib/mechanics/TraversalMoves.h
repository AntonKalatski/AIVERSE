#pragma once
#include <aiverselib/mechanics/Namespace.h>
// traversal-moves: wall-run, wall-slide, wall jump, dash (ground and air charges), grapple hook, glide and
// one-way platforms, layered over LocomotionModes/stepMotion. The collision owner reports the environment of
// this tick (ground, wall contact and normal, grapple ray hit) and the body's current velocity; the policy
// returns the velocity to use when `overrides` is set, plus event bits. It never queries physics or moves bodies.
// Fixed math, integer ticks; one stepTraversal per body per tick with strictly increasing ticks.
// Persistence: genre::save/load (record kind 200); bind traversalConfigHash next to the state.
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum class TraversalMode:uint8_t {None,WallRun,WallSlide,Dash,Grapple,Glide};
enum TraversalEvent:uint32_t {TravWallRunStart=1u<<0,TravWallRunEnd=1u<<1,TravWallSlideStart=1u<<2,TravWallJump=1u<<3,TravDashStart=1u<<4,
    TravDashEnd=1u<<5,TravDashRefused=1u<<6,TravGrappleAttach=1u<<7,TravGrappleRelease=1u<<8,TravGrappleRefused=1u<<9,TravGlideStart=1u<<10,
    TravGlideEnd=1u<<11,TravDropThrough=1u<<12};
struct TraversalConfig {
    bool wallRun=true;Fixed wallRunSpeed=7_fx,wallRunMinSpeed=4_fx,wallRunFall=Fixed::fromRatio(1,2);uint32_t wallRunMaxTicks=90;
    bool wallSlide=true;Fixed wallSlideMaxFall=2_fx;
    Fixed wallJumpPush=6_fx,wallJumpUp=7_fx;uint32_t wallReattachTicks=12;            // same wall cannot be re-run right after a jump
    bool dash=true;Fixed dashSpeed=18_fx,dashExitSpeed=8_fx;uint32_t dashTicks=10,dashCooldownTicks=40,airDashes=1;
    bool grapple=true;Fixed grappleRange=30_fx,grappleSpeed=20_fx,grappleRelease=Fixed::fromRatio(3,2);uint32_t grappleMaxTicks=180;
    bool glide=true;Fixed glideFallSpeed=Fixed::fromRatio(3,2),glideForwardSpeed=8_fx;
    uint32_t dropThroughTicks=15;                                                     // one-way platforms ignore the body this long after drop
};
inline bool valid(const TraversalConfig& c){
    auto pos=[](Fixed v){return v>Fixed{}&&v<=kPolicyMaxSpeed;};
    return pos(c.wallRunSpeed)&&pos(c.wallRunMinSpeed)&&c.wallRunFall>=Fixed{}&&c.wallRunFall<=kPolicyMaxSpeed&&c.wallRunMaxTicks>0&&c.wallRunMaxTicks<=36000
        &&pos(c.wallSlideMaxFall)&&c.wallJumpPush>=Fixed{}&&c.wallJumpPush<=kPolicyMaxSpeed&&c.wallJumpUp>=Fixed{}&&c.wallJumpUp<=kPolicyMaxSpeed&&c.wallReattachTicks<=36000
        &&pos(c.dashSpeed)&&c.dashExitSpeed>=Fixed{}&&c.dashExitSpeed<=c.dashSpeed&&c.dashTicks>0&&c.dashTicks<=600&&c.dashCooldownTicks<=36000&&c.airDashes<=16
        &&pos(c.grappleRange)&&c.grappleRange<=1000_fx&&pos(c.grappleSpeed)&&c.grappleRelease>Fixed{}&&c.grappleMaxTicks>0&&c.grappleMaxTicks<=36000
        &&pos(c.glideFallSpeed)&&c.glideForwardSpeed>=Fixed{}&&c.glideForwardSpeed<=kPolicyMaxSpeed&&c.dropThroughTicks<=600;
}
// Environment of this tick from the collision owner. wallNormal points from the wall to the body (horizontal part used).
// wallId identifies the wall surface (0 = unknown); wallRunnable is the surface tag. grappleHit is the owner's ray hit
// along the aim direction (valid only when hasGrappleHit). onOneWay: standing on a one-way platform.
struct TraversalEnv {bool grounded=false,wall=false,wallRunnable=false,hasGrappleHit=false,onOneWay=false;Vec3 velocity{},position{},wallNormal{},grappleHit{};
    int64_t wallId=0;Vec3 facing{0_fx,{},1_fx};};
// wish: desired horizontal direction (x/z). Pressed flags are edges, held flags are levels.
struct TraversalInput {Vec3 wish{};bool jumpPressed=false,jumpHeld=false,dashPressed=false,grapplePressed=false,grappleReleased=false,downHeld=false;};
struct TraversalState {TraversalMode mode=TraversalMode::None;uint32_t lastTick=0,modeStart=0,dashUntil=0,dashReadyAt=0,dropUntil=0,wallBlockedUntil=0;
    uint32_t airDashesUsed=0;int64_t lastWall=0;Vec3 dashDir{},anchor{};bool initialized=false;};
struct TraversalResult {Vec3 velocity{};bool overrides=false;TraversalMode mode=TraversalMode::None;uint32_t events=0;bool ignoreOneWay=false;};

inline Vec3 wallTangent(Vec3 normal,Vec3 velocity){
    auto n=policyFlat(normal);Vec3 t{-n.z,{},n.x};                                    // cross(up, n)
    return t.dot(velocity)<Fixed{}?Vec3{-t.x,{},-t.z}:t;
}
// One-way platform rule for the collision owner: solid only for a body that was above the top last tick and is not rising,
// and not while a drop-through is active.
inline bool oneWaySolid(Fixed top,Fixed previousFeet,Fixed feet,Fixed velocityY,bool dropping){return !dropping&&velocityY<=Fixed{}&&previousFeet>=top&&feet<=top+Fixed::fromRatio(1,8);}

inline std::optional<TraversalResult> stepTraversal(const TraversalConfig& c,TraversalState& state,const TraversalEnv& env,const TraversalInput& in,uint32_t tick,Fixed dt){
    if(!valid(c)||!policyDtOk(dt)||!policyTickOk(state.initialized,state.lastTick,tick)||!policyBounded(env.velocity,kPolicyMaxSpeed)||!policyBounded(env.position)
        ||!policyBounded(env.grappleHit)||!policyBounded(in.wish,2_fx))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;TraversalResult r;r.velocity=env.velocity;auto& ev=r.events;
    auto setMode=[&](TraversalMode m){s.mode=m;s.modeStart=tick;};
    auto wallJump=[&](){auto n=policyFlat(env.wallNormal);r.velocity=n*c.wallJumpPush;r.velocity.y=c.wallJumpUp;r.overrides=true;ev|=TravWallJump;
        s.lastWall=env.wallId;s.wallBlockedUntil=tick+c.wallReattachTicks;setMode(TraversalMode::None);};
    if(env.grounded){s.airDashesUsed=0;if(s.wallBlockedUntil==UINT32_MAX)s.wallBlockedUntil=0;if(s.mode==TraversalMode::WallRun){ev|=TravWallRunEnd;setMode(TraversalMode::None);}
        if(s.mode==TraversalMode::WallSlide||s.mode==TraversalMode::Glide){if(s.mode==TraversalMode::Glide)ev|=TravGlideEnd;setMode(TraversalMode::None);}}
    // Drop through a one-way platform: hold down and press jump.
    if(env.grounded&&env.onOneWay&&in.downHeld&&in.jumpPressed){s.dropUntil=tick+c.dropThroughTicks;ev|=TravDropThrough;}
    r.ignoreOneWay=tick<s.dropUntil;
    // Grapple release / attach.
    if(s.mode==TraversalMode::Grapple){
        auto to=s.anchor-env.position;auto dist=to.length();
        if(in.grappleReleased||dist<=c.grappleRelease||tick-s.modeStart>=c.grappleMaxTicks){ev|=TravGrappleRelease;setMode(TraversalMode::None);}
        else{r.velocity=fxNormalize(to)*c.grappleSpeed;r.overrides=true;}
    }
    if(c.grapple&&in.grapplePressed&&s.mode!=TraversalMode::Grapple){
        if(env.hasGrappleHit&&(env.grappleHit-env.position).length()<=c.grappleRange){s.anchor=env.grappleHit;setMode(TraversalMode::Grapple);ev|=TravGrappleAttach;
            r.velocity=fxNormalize(s.anchor-env.position)*c.grappleSpeed;r.overrides=true;}
        else ev|=TravGrappleRefused;
    }
    // Dash.
    if(s.mode==TraversalMode::Dash){
        if(tick>=s.dashUntil){r.velocity=s.dashDir*c.dashExitSpeed;r.velocity.y=env.velocity.y<Fixed{}?env.velocity.y:Fixed{};r.overrides=true;ev|=TravDashEnd;setMode(TraversalMode::None);}
        else{r.velocity=s.dashDir*c.dashSpeed;r.overrides=true;}
    }
    if(c.dash&&in.dashPressed&&(s.mode==TraversalMode::Dash||s.mode==TraversalMode::Grapple))ev|=TravDashRefused;
    else if(c.dash&&in.dashPressed){
        bool charge=env.grounded||s.airDashesUsed<c.airDashes;
        if(tick>=s.dashReadyAt&&charge){auto d=policyFlat(in.wish);if(d==Vec3{})d=policyFlat(env.facing);if(d==Vec3{})d={{},{},1_fx};
            s.dashDir=d;s.dashUntil=tick+c.dashTicks;s.dashReadyAt=tick+c.dashCooldownTicks;if(!env.grounded)++s.airDashesUsed;
            if(s.mode==TraversalMode::WallRun)ev|=TravWallRunEnd;if(s.mode==TraversalMode::Glide)ev|=TravGlideEnd;
            setMode(TraversalMode::Dash);r.velocity=d*c.dashSpeed;r.overrides=true;ev|=TravDashStart;}
        else ev|=TravDashRefused;
    }
    bool busy=s.mode==TraversalMode::Dash||s.mode==TraversalMode::Grapple;
    bool sameWallBlocked=env.wallId&&env.wallId==s.lastWall&&tick<s.wallBlockedUntil;
    // Wall-run.
    if(!busy&&s.mode==TraversalMode::WallRun){
        if(!env.wall||!env.wallRunnable||tick-s.modeStart>=c.wallRunMaxTicks){ev|=TravWallRunEnd;if(env.wall){s.lastWall=env.wallId;s.wallBlockedUntil=UINT32_MAX;}setMode(TraversalMode::None);}  // an expired run needs ground before the same wall again
        else if(in.jumpPressed)wallJump();
        else{auto t=wallTangent(env.wallNormal,env.velocity);r.velocity=t*c.wallRunSpeed-policyFlat(env.wallNormal)*Fixed::fromRatio(1,2);r.velocity.y=-c.wallRunFall;r.overrides=true;}
    }else if(!busy&&c.wallRun&&!env.grounded&&env.wall&&env.wallRunnable&&!sameWallBlocked&&s.mode!=TraversalMode::WallSlide){
        auto flat=env.velocity;flat.y={};
        if(flat.length()>=c.wallRunMinSpeed){setMode(TraversalMode::WallRun);ev|=TravWallRunStart;auto t=wallTangent(env.wallNormal,env.velocity);
            r.velocity=t*c.wallRunSpeed;r.velocity.y=-c.wallRunFall;r.overrides=true;}
    }
    // Wall-slide (non-runnable walls or a slow body pressing into the wall).
    if(!busy&&s.mode!=TraversalMode::WallRun&&!(ev&TravWallJump)){
        bool pressing=env.wall&&policyFlat(in.wish).dot(policyFlat(env.wallNormal))<Fixed{};
        if(s.mode==TraversalMode::WallSlide){
            if(!env.wall||env.grounded)setMode(TraversalMode::None);
            else if(in.jumpPressed)wallJump();
            else if(env.velocity.y< -c.wallSlideMaxFall){r.velocity.y=-c.wallSlideMaxFall;r.overrides=true;}
        }else if(c.wallSlide&&!env.grounded&&pressing&&env.velocity.y<Fixed{}&&!sameWallBlocked){setMode(TraversalMode::WallSlide);ev|=TravWallSlideStart;
            if(env.velocity.y< -c.wallSlideMaxFall){r.velocity.y=-c.wallSlideMaxFall;r.overrides=true;}}
    }
    // Glide: hold jump while falling in the air.
    if(!busy&&s.mode==TraversalMode::None&&c.glide&&!env.grounded&&!env.wall&&in.jumpHeld&&!in.jumpPressed&&env.velocity.y<Fixed{}){setMode(TraversalMode::Glide);ev|=TravGlideStart;}
    if(s.mode==TraversalMode::Glide){
        if(!in.jumpHeld||env.grounded||env.wall){ev|=TravGlideEnd;setMode(TraversalMode::None);}
        else{auto d=policyFlat(in.wish);if(d==Vec3{})d=policyFlat(env.facing);r.velocity=d*c.glideForwardSpeed;
            r.velocity.y=env.velocity.y< -c.glideFallSpeed?-c.glideFallSpeed:env.velocity.y;r.overrides=true;}
    }
    r.mode=s.mode;state=s;return r;
}
inline void io(Archive& a,TraversalState& s){a(s.mode,s.lastTick,s.modeStart,s.dashUntil,s.dashReadyAt,s.dropUntil,s.wallBlockedUntil,s.airDashesUsed,s.lastWall,s.dashDir,s.anchor,s.initialized);}
inline uint16_t stateKind(const TraversalState&){return 200;}
inline bool valid(const TraversalState& s){return uint8_t(s.mode)<=uint8_t(TraversalMode::Glide)&&s.modeStart<=s.lastTick&&s.airDashesUsed<=16&&policyBounded(s.dashDir,2_fx)&&policyBounded(s.anchor);}
inline uint64_t traversalConfigHash(const TraversalConfig& c){Hasher h;for(Fixed f:{c.wallRunSpeed,c.wallRunMinSpeed,c.wallRunFall,c.wallSlideMaxFall,c.wallJumpPush,c.wallJumpUp,c.dashSpeed,
    c.dashExitSpeed,c.grappleRange,c.grappleSpeed,c.grappleRelease,c.glideFallSpeed,c.glideForwardSpeed})h.fx(f);
    for(uint32_t v:{c.wallRunMaxTicks,c.wallReattachTicks,c.dashTicks,c.dashCooldownTicks,c.airDashes,c.grappleMaxTicks,c.dropThroughTicks})h.u32(v);
    h.u32(uint32_t(c.wallRun)|uint32_t(c.wallSlide)<<1|uint32_t(c.dash)<<2|uint32_t(c.grapple)<<3|uint32_t(c.glide)<<4);return h.value();}
}
