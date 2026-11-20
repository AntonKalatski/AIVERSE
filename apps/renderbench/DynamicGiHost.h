#pragma once
#include "engine/render/GiMainCpuBridge.h"
#include "engine/render/GiMainGpuBridge.h"
#include "engine/render/Renderer3D.h"
#include <chrono>
namespace aiverse::bench {
// Native host example: immutable opaque geometry captured BEFORE GPU upload,
// explicit indirect-only sources, bounded work before render, per-view binding.
struct DynamicGiHost {
    render::GiMainCpuBridge cpu;
    render::GiMainGpuBridge gpu;
    bool enabled=false,useGpu=false,published=false,converged=false;
    std::string fallback;
    std::shared_ptr<const render::GiChunkedGeometrySnapshot> scene;
    std::vector<render::GiMovingLight> lights;
    render::GiEnvironmentLight environment;
    std::chrono::steady_clock::time_point started;
    double convergenceMs=0;
    static render::float3 vector(const Json& j) {
        if(!j.isArray() || j.size()!=3)throw std::runtime_error("dynamicGI requires xyz vectors");
        render::float3 v{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("dynamicGI nonfinite vector");
        return v;
    }
    void initialize(gfx::Device& d,const Json& settings,render::StaticShadowGeometry geometry,
                    const std::vector<render::PointLight>& sources) {
        using namespace render;
        GiMainCpuConfig cfg;cfg.origin=vector(settings["origin"]);
        const auto dims=vector(settings["dimensions"]);
        for(float n:{dims.x,dims.y,dims.z})if(n<1||n>256||n!=std::floor(n))throw std::runtime_error("dynamicGI dimensions must be integers 1..256");
        cfg.nx=int(dims.x);cfg.ny=int(dims.y);cfg.nz=int(dims.z);
        cfg.spacing=float(settings["spacing"].toDouble(1));
        const double samples=settings["samples"].toDouble(32);
        if(!std::isfinite(samples)||samples<8||samples>128||samples!=std::floor(samples))throw std::runtime_error("dynamicGI samples must be integer 8..128");
        cfg.samples=int(samples);cfg.traceDistance=float(settings["distance"].toDouble(12));
        cfg.cpuBudgetMs=settings["cpuBudgetMs"].toDouble(2);
        GiChunkedGeometryBuilder builder;
        constexpr size_t chunkSize=65536;
        for(size_t first=0;first<geometry.triangles.size();first+=chunkSize) {
            const size_t end=std::min(first+chunkSize,geometry.triangles.size());
            std::vector<StaticShadowGeometry::Triangle> chunk(geometry.triangles.begin()+first,geometry.triangles.begin()+end);
            builder.replace(first/chunkSize+1,std::move(chunk),geometry.albedos);
        }
        scene=std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
        auto decode=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
        for(const auto& source:sources) {
            GiMovingLight l;l.key=lights.size()+1;l.position=source.pos;l.radius=source.radius;l.intensity=source.intensity;
            l.color={decode(source.color.x),decode(source.color.y),decode(source.color.z)};
            l.direction=source.dir;l.cosInner=source.cosInner;l.cosOuter=source.cosOuter;lights.push_back(l);
        }
        if(settings.has("sunDirection"))environment.sunDirection=normalize(vector(settings["sunDirection"]));
        if(settings.has("sunColor"))environment.sunColor=vector(settings["sunColor"]);
        if(settings.has("skyColor"))environment.skyColor=vector(settings["skyColor"]);
        environment.sunIntensity=float(settings["sunIntensity"].toDouble(0));
        environment.skyIntensity=float(settings["skyIntensity"].toDouble(0));
        if(!environment.valid())throw std::runtime_error("invalid dynamicGI indirect environment");
        const auto backend=settings.getStr("backend","auto");
        if(backend!="auto"&&backend!="cpu"&&backend!="gpu")throw std::runtime_error("dynamicGI backend must be auto, cpu or gpu");
        const bool fits=scene->triangleCount()<=GiGpuSmallScene::kMaxTriangles && lights.size()<=GiGpuSmallScene::kMaxLights && cfg.samples<=GiGpuSmallScene::kMaxSamples;
        if(backend!="cpu") {
            if(!fits)fallback="bounded GPU capacity exceeded; full scene retained on CPU";
            else if(!d.supportsCompute())fallback="device has no compute; using CPU";
            else if(!(useGpu=gpu.initialize(d,cfg)))fallback="GPU initialization rejected; using CPU";
            if(!useGpu && backend=="gpu")throw std::runtime_error(fallback);
        }
        if(!useGpu && !cpu.initialize(d,cfg))throw std::runtime_error("dynamicGI CPU initialization failed");
        enabled=true;
    }
    bool update(gfx::Device& d,render::float3 focus) {
        if(!enabled)return true;
        if(!d.gpuHealthy())throw std::runtime_error("dynamicGI device lost");
        if(!published) {
            started=std::chrono::steady_clock::now();
            const bool ok=useGpu?(gpu.publishGeometry(scene,1)&&gpu.publishLights(lights)&&gpu.publishEnvironment(environment)):
                (cpu.publishGeometry(scene,1)&&cpu.publishLights(lights)&&cpu.publishEnvironment(environment));
            if(!ok)throw std::runtime_error("dynamicGI source publication failed");
            published=true;
        }
        if(useGpu){gpu.updateFrame(focus);converged=gpu.canPresent();}
        else {const auto stats=cpu.updateFrame(focus);if(!stats.canPresent)throw std::runtime_error("dynamicGI CPU update failed");converged=stats.pending==0;}
        if(!converged && std::chrono::steady_clock::now()-started>std::chrono::seconds(60))throw std::runtime_error("dynamicGI convergence timeout");
        if(converged && !convergenceMs)convergenceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return converged;
    }
    bool attach(render::Renderer3D& r) {
        return !enabled || (converged && (useGpu?r.setDynamicGiOverlay(gpu):r.setDynamicGiOverlay(cpu)));
    }
    bool shutdownAfterGpuIdle(gfx::Device& d,render::Renderer3D& r) {
        r.clearDynamicGiOverlay();
        if(!enabled && !scene)return true;
        if(!d.waitForResourceIdle())return false;
        const bool ok=gpu.shutdownAfterGpuIdle() && cpu.shutdownAfterGpuIdle();
        scene.reset();enabled=false;return ok;
    }
};
}
