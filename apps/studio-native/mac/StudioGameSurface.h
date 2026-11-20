#pragma once
#include "apps/client/HostedClient.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "engine/data/Json.h"

namespace aiverse::studio {
// Independent native surface; the authoring Scene keeps its own device/epoch.
class StudioGameSurface final {
public:
    explicit StudioGameSurface(void* layer);
    ~StudioGameSurface();
    void start(const StudioGameLaunch& plan,const std::string& engineAssets);
    void stop();
    void frame(int width,int height,float pointsW,float pointsH,double dt,bool focused);
    void suspend();
    void event(const platform::Event& event);
    Json state() const;
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> lifetime_;
    std::unique_ptr<client::HostedClient> host_;
    std::string error_;
};
}
