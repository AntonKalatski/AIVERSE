#include "StudioGameSurface.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
StudioGameSurface::StudioGameSurface(void* layer):device_(gfx::createMetalDevice(layer)) {
    if (!device_) throw std::runtime_error("Game Metal device unavailable");
    lifetime_=std::make_unique<render::RenderDeviceLifetime>(*device_);
    host_=std::make_unique<client::HostedClient>();
}
StudioGameSurface::~StudioGameSurface() {
    // HostedClient stops and drains by a real GPU idle/fence before device dies.
    host_.reset();
    lifetime_->retire();
}
void StudioGameSurface::start(const StudioGameLaunch& plan,const std::string& assets) {
    error_.clear();
    host_->start(*device_,plan.project,assets,lifetime_->lease(),plan.map);
}
void StudioGameSurface::stop() { host_->suspendInput();host_->stop();host_->collectGpuRetirements(); }
void StudioGameSurface::suspend() { host_->suspendInput();host_->collectGpuRetirements(); }
void StudioGameSurface::event(const platform::Event& event) {
    if (!host_->active()) return;
    if (event.type==platform::EventType::FocusLost) { suspend();return; }
    if (event.type==platform::EventType::MouseDown || event.type==platform::EventType::FocusGained)
        host_->place(0,0,1,true);
    host_->event(event);
}
void StudioGameSurface::frame(int w,int h,float pw,float ph,double dt,bool focused) {
    host_->collectGpuRetirements();
    if (!host_->active() || w<2 || h<2) return;
    host_->place(0,0,1,focused);
    device_->beginFrame(w,h);
    try {
        // A loading step may not draw yet; initialize the actual drawable.
        gfx::PassDesc clear;device_->beginPass(clear);device_->endPass();
        host_->frame(std::clamp(dt,0.0,0.1),{}, {},w,h,pw,ph,false,{});
        device_->endFrame();
        if (host_->quitRequested()) stop();
        if (!host_->errorText().empty()) error_=host_->errorText();
    } catch (const std::exception& e) {
        device_->endPass();device_->endFrame();error_=e.what();stop();
    }
}
Json StudioGameSurface::state() const {
    Json out=Json::object();
    out.set("active",Json::fromBool(host_->active()));
    out.set("loading",Json::fromBool(host_->loading()));
    out.set("progress",Json::number(double(host_->progress())));
    out.set("inMatch",Json::fromBool(host_->inMatch()));
    out.set("error",Json::string(error_));
    auto* sim=host_->simulation();
    out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
    return out;
}
}
