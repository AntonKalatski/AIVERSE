// AIVERSE LENS: SDK-independent bridge for runtime-provided XR eye views.
// All poses and FOV tangents use AIVERSE's right-handed +Y-up, -Z-forward space.
// An OpenXR host must convert its coordinate conventions before calling this API.
// This builds ordinary per-eye CameraCaptureView descriptions; swapchain image
// import, late-latching, reprojection and frame submission belong to the host.
#pragma once
#include "CameraCaptureViews.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

struct CameraXrEye {
    float3 position{};             // predicted world-space eye position
    quat orientation{};            // predicted eye-to-world rotation
    float leftTan=-1.f, rightTan=1.f;
    float downTan=-1.f, upTan=1.f; // visible ray slopes in eye space
    uint64_t targetAssetKey=0;
    int width=0, height=0;
    bool hdr=false;
};

struct CameraXrFrame {
    std::array<CameraXrEye,2> eyes{};
    double predictedDisplayTime=0;
};

// Return false without modifying output on invalid runtime data. In particular,
// normal Camera::proj derives horizontal scale from target pixel aspect, so
// unsupported FOV/size combinations must not silently distort XR images.
inline bool makeXrCameraViews(const CameraXrFrame& frame,
                              const Camera& cameraTemplate,
                              std::array<CameraCaptureView,2>& output) {
    if (!std::isfinite(frame.predictedDisplayTime)) return false;
    std::array<CameraCaptureView,2> result{};
    for (size_t i=0;i<2;++i) {
        const auto& eye=frame.eyes[i];
        if (eye.width<=0 || eye.height<=0 || eye.targetAssetKey==0 ||
            frame.eyes[1-i].targetAssetKey==eye.targetAssetKey) return false;
        const auto finite=[](float n){return std::isfinite(n);};
        if (!finite(eye.position.x)||!finite(eye.position.y)||!finite(eye.position.z)||
            !finite(eye.orientation.x)||!finite(eye.orientation.y)||
            !finite(eye.orientation.z)||!finite(eye.orientation.w)||
            !finite(eye.leftTan)||!finite(eye.rightTan)||
            !finite(eye.downTan)||!finite(eye.upTan)) return false;
        const float q2=eye.orientation.x*eye.orientation.x+
                       eye.orientation.y*eye.orientation.y+
                       eye.orientation.z*eye.orientation.z+
                       eye.orientation.w*eye.orientation.w;
        const float horizontal=eye.rightTan-eye.leftTan;
        const float vertical=eye.upTan-eye.downTan;
        if (!finite(q2)||q2<1.e-8f || !(horizontal>0) || !(vertical>0) ||
            !(eye.leftTan<0 && eye.rightTan>0 && eye.downTan<0 && eye.upTan>0))
            return false;
        const float requiredAspect=horizontal/vertical;
        const float pixelAspect=float(eye.width)/float(eye.height);
        if (!finite(requiredAspect)||!finite(pixelAspect)||
            std::abs(requiredAspect/pixelAspect-1.f)>0.01f) return false;
        Camera camera=cameraTemplate;
        camera.setPose(eye.position,eye.orientation);
        camera.ortho=false;
        camera.fovY=2.f*std::atan(vertical*.5f);
        camera.lensShift={-(eye.rightTan+eye.leftTan)/horizontal,
                          -(eye.upTan+eye.downTan)/vertical};
        camera.jitter={};
        camera.obliqueClip=false;
        if (!(camera.zNear>0.f) || !(camera.zFar>camera.zNear)) return false;
        result[i]={camera,eye.targetAssetKey,eye.width,eye.height,eye.hdr};
    }
    output=result;
    return true;
}
} // namespace aiverse::render
