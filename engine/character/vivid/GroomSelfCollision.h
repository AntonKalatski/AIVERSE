// VIVID deterministic, bounded inter-guide strand collision.
// Position-based correction on groom-local particles. No ECS/RHI dependency.
#pragma once
#include "HumanGroom.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace aiverse::vivid {
class GroomSelfCollision final {
    struct Cell {
        int x=0,y=0,z=0;
        bool operator<(const Cell& b) const noexcept {
            if(x!=b.x)return x<b.x;
            if(y!=b.y)return y<b.y;
            return z<b.z;
        }
    };
    struct Particle {
        std::uint32_t guide=0,segment=0;
        render::float3 point{};
    };
    static bool valid(render::float3 p) noexcept {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
public:
    static bool validate(const GroomSelfCollisionConfig& c) noexcept {
        return std::isfinite(c.separation)&&c.separation>=0.001f&&
            c.separation<=0.1f&&std::isfinite(c.response)&&
            c.response>=0&&c.response<=1&&
            c.maxPairChecks>=1024&&c.maxPairChecks<=2000000&&
            std::isfinite(c.friction)&&c.friction>=0&&c.friction<=1&&
            std::isfinite(c.bendPreservation)&&
            c.bendPreservation>=0&&c.bendPreservation<=1;
    }
    // Called after each Verlet constraint solve. Root particles remain pinned.
    // Each unordered cross-guide pair appears once (lexicographic grid order).
    // Uses a Jacobi correction buffer so iteration order cannot create bias.
    static bool resolve(std::span<GroomGuide> guides,
                        const GroomSelfCollisionConfig& cfg,
                        GroomSelfCollisionStats& stats) {
        stats={};
        if(!validate(cfg))return false;
        if(!cfg.enabled || cfg.response==0 || guides.size()<2)return true;
        std::vector<Particle> particles;
        for(std::size_t g=0;g<guides.size();++g) {
            const auto& guide=guides[g];
            if(guide.position.size()!=guide.previous.size()||
               guide.position.size()!=guide.rest.size() ||
               guide.position.empty()||
               g>std::numeric_limits<std::uint32_t>::max())return false;
            for(std::size_t j=1;j<guide.position.size();++j) {
                if(!valid(guide.position[j]) ||
                   j>std::numeric_limits<std::uint32_t>::max())return false;
                if(particles.size()>=131072)return false;
                particles.push_back({std::uint32_t(g),std::uint32_t(j),
                                     guide.position[j]});
            }
        }
        const float size=cfg.separation;
        const auto cellOf=[&](render::float3 p,Cell& cell) {
            const float cx=std::floor(p.x/size),cy=std::floor(p.y/size),
                        cz=std::floor(p.z/size);
            constexpr float limit=1000000.f;
            if(!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(cz)||
               std::fabs(cx)>limit||std::fabs(cy)>limit||
               std::fabs(cz)>limit)return false;
            cell={int(cx),int(cy),int(cz)};
            return true;
        };
        std::map<Cell,std::vector<std::size_t>> grid;
        std::vector<Cell> cells(particles.size());
        for(std::size_t i=0;i<particles.size();++i) {
            if(!cellOf(particles[i].point,cells[i]))return false;
            grid[cells[i]].push_back(i);
        }
        std::vector<render::float3> corrections(particles.size());
        std::vector<std::uint16_t> counts(particles.size());
        const float distance2=size*size;
        bool exhausted=false;
        for(std::size_t i=0;i<particles.size()&&!exhausted;++i) {
            const auto cell=cells[i];
            for(int dx=-1;dx<=1&&!exhausted;++dx)
                for(int dy=-1;dy<=1&&!exhausted;++dy)
                    for(int dz=-1;dz<=1&&!exhausted;++dz) {
                        const auto found=grid.find(
                            {cell.x+dx,cell.y+dy,cell.z+dz});
                        if(found==grid.end())continue;
                        for(auto j:found->second) {
                            if(j<=i || particles[i].guide==particles[j].guide)
                                continue; // only unique cross-guide pairs
                            if(stats.pairChecks>=cfg.maxPairChecks) {
                                exhausted=true;
                                stats.budgetHit=true;
                                break;
                            }
                            ++stats.pairChecks;
                            const auto d=particles[j].point-particles[i].point;
                            const float squared=render::dot(d,d);
                            if(!(squared<distance2) || !std::isfinite(squared))
                                continue;
                            const float len=std::sqrt(std::max(0.f,squared));
                            // Deterministic fallback when two nodes coincide.
                            const render::float3 axis=((i+j)&1u)?
                                render::float3{1,0,0}:render::float3{0,0,1};
                            const auto normal=len>1e-7f?d*(1.f/len):axis;
                            const float depth=(size-len)*cfg.response*0.5f;
                            corrections[i]=corrections[i]-normal*depth;
                            corrections[j]+=normal*depth;
                            if(counts[i]<65535)++counts[i];
                            if(counts[j]<65535)++counts[j];
                            ++stats.contacts;
                        }
                    }
        }
        // Restrict corrections under extreme clumps. Scalp collision and
        // strand-length constraints in the parent solver remain authoritative.
        for(std::size_t i=0;i<particles.size();++i) {
            if(!counts[i])continue;
            const auto& p=particles[i];
            auto offset=corrections[i]*(1.f/float(counts[i]));
            const float distance=render::length(offset);
            if(distance>size)offset=offset*(size/distance);
            const auto candidate=guides[p.guide].position[p.segment]+offset;
            if(!valid(candidate))return false;
        }
        for(std::size_t i=0;i<particles.size();++i) {
            if(!counts[i])continue;
            const auto& p=particles[i];
            auto offset=corrections[i]*(1.f/float(counts[i]));
            const float distance=render::length(offset);
            if(distance>size)offset=offset*(size/distance);
            auto& guide=guides[p.guide];
            guide.position[p.segment]+=offset;
            // Inelastic collision: do not inject a spurious Verlet impulse.
            guide.previous[p.segment]+=offset;
        }
        return true;
    }
};
} // namespace aiverse::vivid
