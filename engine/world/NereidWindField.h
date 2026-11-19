// NEREID Stage 33 — spatial presentation-only wind field.
// PHYSIS (or the host weather source) remains authoritative; the native
// host publishes immutable wind zones after its weather read barrier.
#pragma once
#include "engine/world/NereidWindPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::world {

struct NereidWindZone {
    double x=0, z=0;             // world X/Z, double for far-world precision
    double radius=100;           // metres; no allocation per tile or pixel
    NereidVisualWind wind{};     // local weather sample
    float influence=1.f;         // 0..1 relative weight of the wind zone
};

class NereidVisualWindField final {
public:
    static constexpr size_t kMaxZones=64;
    void setBase(NereidVisualWind wind) {
        base_=normalizeNereidWind(wind);
    }
    const NereidVisualWind& base()const{return base_;}
    void setZones(std::vector<NereidWindZone> incoming) {
        if(incoming.size()>kMaxZones)
            throw std::invalid_argument("nereid.wind: too many wind zones");
        for(auto& zone:incoming) {
            if(!std::isfinite(zone.x) || !std::isfinite(zone.z) ||
               !std::isfinite(zone.radius) || zone.radius<=0 ||
               zone.radius>1000000.0 || !std::isfinite(zone.influence) ||
               zone.influence<0.f || zone.influence>1.f)
                throw std::invalid_argument("nereid.wind: invalid zone");
            zone.wind=normalizeNereidWind(zone.wind);
        }
        zones_=std::move(incoming);
    }
    const std::vector<NereidWindZone>& zones()const{return zones_;}
    NereidVisualWind sample(double x,double z)const {
        if(!std::isfinite(x) || !std::isfinite(z))return base_;
        double total=0,ex=0,nz=0,g=0,c=0;
        for(const auto& zone:zones_) {
            const double dx=(x-zone.x)/zone.radius;
            const double dz=(z-zone.z)/zone.radius;
            const double d2=dx*dx+dz*dz;
            if(d2>=1.0)continue;
            const double t=std::clamp(1.0-std::sqrt(d2),0.0,1.0);
            const double weight=double(zone.influence)*t*t*(3.0-2.0*t);
            if(weight<=0)continue;
            total+=weight;
            ex+=weight*zone.wind.east;
            nz+=weight*zone.wind.north;
            g+=weight*zone.wind.gust;
            c+=weight*zone.wind.coupling;
        }
        if(total<=0)return base_;
        const float blend=float(std::min(total,1.0));
        const float inv=float(1.0/total);
        return normalizeNereidWind({
            base_.east+(ex*inv-base_.east)*blend,
            base_.north+(nz*inv-base_.north)*blend,
            base_.gust+(g*inv-base_.gust)*blend,
            base_.coupling+(c*inv-base_.coupling)*blend});
    }
private:
    NereidVisualWind base_{};
    std::vector<NereidWindZone> zones_;
};
} // namespace aiverse::world
