// Camera modes of the game views (ECS camera rigs and rails: CameraRig.h), all producing a render::Camera:
//   overhead - the data-driven map camera, game.json "camera" (CameraSetup, applyCameraSetup);
//   chase    - behind and above a vehicle (chaseCamera);
//   first person - eyes of an actor with mouse look (Look).
// The orbit / free camera of the editor and planet views is render::Camera itself.
//
//   "camera": {"projection": "ortho", "pitch": 90, "yaw": 0, "fov": 43,
//              "zoom": 18, "zoomMin": 8, "zoomMax": 40, "pixelsPerCell": 32, "rotate": false}
//
// pitch/yaw/fov in degrees. Zoom values: visible map cells (vertically) for "ortho", camera
// distance for "perspective". "pixelsPerCell" (ortho) sets the start zoom so one cell is that many
// framebuffer pixels (pixel-perfect sprites). Without the block the engine default camera is kept.
#pragma once
#include <algorithm>
#include <cmath>
#include "../data/Json.h"
#include "Math3D.h"

namespace aiverse::render {

// ---- overhead
struct CameraSetup {
    bool present = false, ortho = false, rotate = true;
    float pitch = 0.95f, yaw = 0, fovY = 0.75f;      // radians
    float zoom = -1, zoomMin = -1, zoomMax = -1;    // in the units above, -1 = keep
    float pixelsPerCell = 0;
};

inline CameraSetup cameraSetupFrom(const Json& manifest) {
    CameraSetup c;
    const Json& j = manifest["camera"];
    if (!j.isObject()) return c;
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    auto num = [&](const char* k, float def) { return j[k].isNumber() ? float(j[k].fx().toDouble()) : def; };
    c.present = true;
    c.ortho = j.getStr("projection", "perspective") == "ortho";
    c.pitch = std::clamp(num("pitch", 54.4f), 5.0f, 90.0f) * kDeg;
    c.yaw = num("yaw", 0) * kDeg;
    c.fovY = std::clamp(num("fov", 43.0f), 5.0f, 120.0f) * kDeg;
    c.zoom = num("zoom", -1); c.zoomMin = num("zoomMin", -1); c.zoomMax = num("zoomMax", -1);
    c.pixelsPerCell = num("pixelsPerCell", 0);
    c.rotate = j.getBool("rotate", !c.ortho);
    return c;
}

// Converts a zoom value of the block into Camera::distance.
inline float cameraDistanceFor(const CameraSetup& c, float zoom) {
    return c.ortho ? zoom * 0.5f / std::tan(c.fovY * 0.5f) : zoom;
}

// Applies projection, angles and zoom limits. Sets the zoom when the block gives one (zoom or
// pixelsPerCell) and returns true; otherwise the caller's framing distance stays.
inline bool applyCameraSetup(const CameraSetup& c, Camera& cam, float viewHeightPx) {
    if (!c.present) return false;
    cam.ortho = c.ortho;
    cam.pitch = c.pitch;
    cam.yaw = c.yaw;
    cam.fovY = c.fovY;
    if (c.zoomMin > 0) cam.minDistance = cameraDistanceFor(c, c.zoomMin);
    if (c.zoomMax > 0) cam.maxDistance = cameraDistanceFor(c, c.zoomMax);
    float zoom = c.zoom;
    if (c.ortho && c.pixelsPerCell > 0 && viewHeightPx > 0) zoom = viewHeightPx / c.pixelsPerCell;
    if (zoom <= 0) { cam.distance = std::clamp(cam.distance, cam.minDistance, cam.maxDistance); return false; }
    cam.distance = std::clamp(cameraDistanceFor(c, zoom), cam.minDistance, std::max(cam.minDistance, cam.maxDistance));
    return true;
}


// ---- chase. `pos` in local render space, `yawTurns` the heading in the vehicle controllers' convention
// (0 faces +Z, a quarter turn faces +X). Presentation only.
inline Camera chaseCamera(float3 pos,float yawTurns,float distance=11.f,float height=1.4f,float pitch=0.26f){
    constexpr float tau=6.28318530718f;
    float3 forward{std::sin(yawTurns*tau),0,std::cos(yawTurns*tau)};
    Camera c;c.target=pos+forward*2.5f+float3{0,height,0};c.distance=distance;c.pitch=pitch;c.yaw=yawTurns*tau+tau*0.5f;
    c.fovY=1.05f;c.zNear=0.1f;c.zFar=1500;return c;
}
// Shortest-arc interpolation of a heading in turns (between two snapshots).
inline float lerpTurns(float a,float b,float t){float d=std::fmod(b-a+1.5f,1.0f)-0.5f;return a+d*t;}

// ---- first person
struct Look {
    float yaw=0,pitch=0; // turns
    void motion(float dx,float dy,float sensitivity=0.0004f,bool invertY=false) {
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(sensitivity)||sensitivity<0)return;
        // lookAt uses right = cross(forward, up): facing +Z, screen-right is -X.
        yaw=std::fmod(yaw-dx*sensitivity,1.0f);if(yaw<0)yaw+=1.0f;
        pitch=std::clamp(pitch+(invertY?dy:-dy)*sensitivity,-0.248f,0.248f);
    }
    Camera camera(float3 eye) const {
        constexpr float tau=6.28318530718f;
        auto cp=std::cos(pitch*tau);
        float3 forward{std::sin(yaw*tau)*cp,std::sin(pitch*tau),std::cos(yaw*tau)*cp};
        Camera result;result.distance=1;result.pitch=-pitch*tau;result.yaw=yaw*tau+tau*0.5f;
        result.target=eye+forward;result.fovY=1.22f;result.zNear=0.03f;result.zFar=1000;return result;
    }
};

} // namespace aiverse::render
