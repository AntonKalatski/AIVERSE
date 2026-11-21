// TerraWeave Arbor CPU render-mesh builder (presentation only).
// Converts deterministic local-space tree skeletons to bark tubes + leaf cards.
// GPU resources, shaders, LOD impostor baking and RHI uploads belong to adapters.
#pragma once
#include "engine/world/TerraWeaveArbor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct ArborVertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 1, nz = 0;
    float u = 0, v = 0;
    uint8_t material = 0; // 0 bark, 1 leaves
};

struct ArborMesh {
    std::vector<ArborVertex> vertices;
    std::vector<uint32_t> indices;
};

struct ArborMeshSettings {
    uint32_t radialSegments = 6; // 3..16
    bool doubleSidedLeaves = true;
    uint32_t leafPlanes = 2; // 1: one card, 2: crossed cards
    bool naturalConifer = false; // opt-in 2x2 needle-spray atlas, cosmetic only
    uint32_t longitudinalSegments = 6; // 1..12, curved tube tessellation
    float sprayDroop = .18f; // downward shoot angle, presentation only
    bool compactConifer = false; // bounded volumetric sprays and trunk flare
    uint32_t sprayPairs = 7; // 2..12, retained branch silhouette at every LOD
    bool naturalBroadleaf = false; // connected2x2 spray atlas on owned twigs
    uint32_t leafCoveragePermille = 1000; // existing ecology/phenology input
    bool trunkFlare = true, includeLeader = true; // isolated branch adapters set these explicitly
};

namespace arbor_detail {
struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float f) { return {a.x * f, a.y * f, a.z * f}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline V3 normalize(V3 v) {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 < 1.0e-12f) return {0, 1, 0};
    return v * (1.0f / std::sqrt(len2));
}
inline void addTriangle(ArborMesh& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}
inline void addLeafQuad(ArborMesh& mesh, V3 center, V3 side, float radius,
                        bool doubleSided) {
    const V3 up{0, radius, 0};
    const V3 horizontal = side * radius;
    const V3 n = normalize(cross(horizontal, up));
    const V3 coords[4] = {
        center - horizontal - up, center + horizontal - up,
        center + horizontal + up, center - horizontal + up
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i)
        mesh.vertices.push_back({coords[i].x, coords[i].y, coords[i].z,
                                 n.x, n.y, n.z, uv[i][0], uv[i][1], 1});
    addTriangle(mesh, base, base + 1, base + 2);
    addTriangle(mesh, base, base + 2, base + 3);
    if (doubleSided) {
        addTriangle(mesh, base + 2, base + 1, base);
        addTriangle(mesh, base + 3, base + 2, base);
    }
}
} // namespace arbor_detail

inline ArborMesh buildArborMesh(const TreeSkeleton& tree,
                               ArborMeshSettings settings = {}) {
    if (settings.radialSegments < 3 || settings.radialSegments > 16 ||
        settings.leafPlanes < 1 || settings.leafPlanes > 2 ||
        settings.sprayPairs < 1 || settings.sprayPairs > 12 ||
        (!std::isfinite(settings.sprayDroop)||settings.sprayDroop<0||settings.sprayDroop>1) ||
        (settings.compactConifer && !settings.naturalConifer) ||
        settings.longitudinalSegments<1 || settings.longitudinalSegments>12 ||
        settings.leafCoveragePermille>1000 || (settings.naturalConifer&&settings.naturalBroadleaf))
        throw std::invalid_argument("TerraWeave Arbor radial segments");
    ArborMesh out;
    constexpr float kTau = 6.2831853071795864769f;
    using namespace arbor_detail;
    for (const TreeBranch& b : tree.branches) {
        // At far detail the bough card's textured stem provides the twig;
        // subpixel branch tubes would cost more than the foliage itself.
        if(settings.compactConifer&&settings.sprayPairs==1&&b.parent>=0)continue;
        V3 start{float(b.x0), float(b.y0), float(b.z0)};
        V3 end{float(b.x1), float(b.y1), float(b.z1)};
        const V3 delta = end - start;
        const float length = std::sqrt(delta.x * delta.x +
                                       delta.y * delta.y + delta.z * delta.z);
        if (length <= 0.001f) continue;
        const V3 axis = delta * (1.0f / length);
        const V3 helper = std::fabs(axis.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 tangent = normalize(cross(axis, helper));
        const V3 bitangent = normalize(cross(axis, tangent));
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        const uint32_t side = settings.radialSegments + 1u;
        const uint32_t rings = (settings.naturalConifer||settings.naturalBroadleaf) ? settings.longitudinalSegments : 1;
        for (uint32_t ring = 0; ring <= rings; ++ring) {
            const float step = float(ring) / rings;
            const float t = (settings.compactConifer||settings.naturalBroadleaf)&&b.parent<0?step*step*step:step;
            const float sag = settings.naturalConifer && std::fabs(axis.y)<0.95f ? length*.12f : 0.f;
            const bool flare=settings.trunkFlare&&(settings.compactConifer||settings.naturalBroadleaf)&&b.parent<0;
            const float root=flare?std::exp(-t*12.f):0.f;
            const V3 center = start + delta*t + V3{0,-sag*std::sin(kTau*.5f*t)-15.f*root,0};
            const float radius = (float(b.startRadiusCm)*(1-t) + float(b.endRadiusCm)*t) *
                (settings.naturalConifer && std::fabs(axis.y)<.95f ? .30f : 1.f)*(1.f+.65f*root);
            for (uint32_t seg = 0; seg <= settings.radialSegments; ++seg) {
                const float angle = kTau * float(seg) / float(settings.radialSegments);
                const V3 normal = tangent * std::cos(angle) + bitangent * std::sin(angle);
                const V3 at = center + normal * radius;
                out.vertices.push_back({at.x, at.y, at.z,
                                        normal.x, normal.y, normal.z,
                                        float(seg) / float(settings.radialSegments),
                                        t * length / 100.0f, 0});
            }
        }
        for (uint32_t ring=0; ring<rings; ++ring)
        for (uint32_t seg = 0; seg < settings.radialSegments; ++seg) {
            const uint32_t a = base + ring*side + seg, b0 = a + 1u;
            const uint32_t c = a + side, d = c + 1u;
            addTriangle(out, a, c, b0);
            addTriangle(out, b0, c, d);
        }
    }
    if(settings.naturalBroadleaf) {
        // Every spray's stem is on its owning woody twig. Two atlas cells have
        // vertical stems; diagonal cells need a different authored anchor.
        for(const auto& leaf:tree.leaves) {
            if(leaf.variation%1000>=settings.leafCoveragePermille)continue;
            if(leaf.parentBranch<0||size_t(leaf.parentBranch)>=tree.branches.size())continue;
            const auto& branch=tree.branches[size_t(leaf.parentBranch)];
            const V3 axis=normalize(V3{float(branch.x1-branch.x0),float(branch.y1-branch.y0),float(branch.z1-branch.z0)});
            const V3 leafAt{float(leaf.xCm),float(leaf.yCm),float(leaf.zCm)};
            const V3 branchStart{float(branch.x0),float(branch.y0),float(branch.z0)};
            const V3 branchDelta{float(branch.x1-branch.x0),float(branch.y1-branch.y0),float(branch.z1-branch.z0)};
            const float branchLength2=branchDelta.x*branchDelta.x+branchDelta.y*branchDelta.y+branchDelta.z*branchDelta.z;
            const V3 fromStart=leafAt-branchStart;
            const float leafT=branchLength2>.001f?(fromStart.x*branchDelta.x+fromStart.y*branchDelta.y+fromStart.z*branchDelta.z)/branchLength2:1.f;
            const V3 helper=std::abs(axis.y)<.9f?V3{0,1,0}:V3{1,0,0};
            const V3 side=normalize(cross(axis,helper));
            const float length=float(leaf.radiusCm)*2.f;
            for(uint32_t plane=0;plane<settings.leafPlanes*settings.sprayPairs;++plane) {
                const uint32_t spray=plane/settings.leafPlanes;
                const float at=std::clamp(leafT+(float(spray)-float(settings.sprayPairs-1)*.5f)*.10f,.25f,1.f);
                const V3 anchor=branchStart+branchDelta*at;
                const uint64_t shoot=detail::mix64(uint64_t(leaf.variation)^uint64_t(spray+1)*7919u);
                const float lateral=(float(shoot%1001)/1000.f-.5f)*1.3f;
                const float rise=float((shoot>>16)%1001)/1000.f*.9f-.15f;
                const V3 forward=normalize(axis*.65f+side*lateral+V3{0,rise-1.1f*settings.sprayDroop,0});
                const V3 spraySide=normalize(cross(forward,std::abs(forward.y)<.9f?V3{0,1,0}:V3{1,0,0}));
                const float angle=float(leaf.variation%6283)*.001f+plane*1.5707963f;
                const V3 across=normalize(spraySide*std::cos(angle)+cross(forward,spraySide)*std::sin(angle));
                const V3 face=normalize(cross(across,forward));
                const V3 points[4]={anchor-across*length*.38f,anchor+across*length*.38f,
                    anchor+forward*length+across*length*.38f,anchor+forward*length-across*length*.38f};
                const uint32_t base=uint32_t(out.vertices.size());
                const float v=float((leaf.variation>>8)&1)*.5f;
                const float uv[4][2]={{.006f,v+.494f},{.494f,v+.494f},{.494f,v+.006f},{.006f,v+.006f}};
                for(int k=0;k<4;++k) {
                    const V3 n=normalize(face*.35f+normalize(V3{points[k].x,tree.heightCm*.2f,points[k].z})*.65f);
                    out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});
                }
                addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
            }
        }
        return out;
    }
    if (settings.naturalConifer) {
        // The same structural branches feed all tiers. Sprays grow from their
        // curved centreline, never from a disconnected radial star at a leaf.
        for (size_t bi=0; bi<tree.branches.size(); ++bi) {
            const auto& b=tree.branches[bi];
            const V3 start{float(b.x0),float(b.y0),float(b.z0)};
            const V3 delta{float(b.x1-b.x0),float(b.y1-b.y0),float(b.z1-b.z0)};
            const float len=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            if(len<2 || std::fabs(delta.y)>len*.95f)continue;
            const V3 axis=normalize(delta), side=normalize(cross(axis,{0,1,0}));
            const uint64_t h=detail::mix64(tree.seed ^ uint64_t(b.y0)*7919 ^ uint64_t(b.x1)*104729);
            if(h%1000>=settings.leafCoveragePermille)continue;
            if(settings.compactConifer&&settings.sprayPairs==1) {
                // Far detail represents a whole foliated bough, not two tiny
                // shoots. Keep projected cover while reducing needle geometry.
                const V3 anchor=start;
                const V3 forward=normalize(axis+V3{0,-settings.sprayDroop*.12f,0});
                const float length=len+65.f,width=std::clamp(len*.48f+65.f,70.f,240.f);
                for(uint32_t plane=0;plane<2;++plane) {
                    const V3 across=plane?normalize(cross(forward,side)):normalize(side+V3{0,.25f,0});
                    const V3 normal=normalize(cross(across,forward));
                    const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                        anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                    const uint32_t cell=uint32_t((h>>(plane*8))%4),base=uint32_t(out.vertices.size());
                    const float u=float(cell%2)*.5f,v=float(cell/2)*.5f;
                    const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                    for(int k=0;k<4;++k){const V3 radial=normalize(V3{points[k].x,tree.heightCm*.12f,points[k].z});const V3 n=normalize(normal*.35f+radial*.65f);
                        out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});}
                    addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                    if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
                }
                continue;
            }
            for(uint32_t j=0;j<settings.sprayPairs;++j) {
                const float t=settings.sprayPairs==1?.86f:settings.compactConifer&&settings.sprayPairs==3?.18f+.70f*float(j)/2.f:.20f+.78f*float(j)/float(settings.sprayPairs-1);
                const V3 anchor=start+delta*t+V3{0,-len*.12f*std::sin(kTau*.5f*t),0};
                for(int sign : {-1,1}) {
                    const uint64_t leafHash=detail::mix64(h+j*97+uint64_t(sign+1));
                    const float length=(settings.compactConifer?
                        std::clamp(len*(.38f-.14f*t),std::clamp((float(tree.heightCm)-anchor.y)*.25f,22.f,65.f),145.f)*(settings.sprayPairs==1?1.25f:1.f):std::clamp(len*(.60f-.30f*t),30.f,170.f))*(settings.compactConifer&&settings.sprayPairs==3?1.20f:1.f);
                    const V3 forward=normalize(axis*.70f+side*(float(sign)*.72f)+V3{0,-settings.sprayDroop,0});
                    // Tilt breaks the paper-flat horizontal shelf silhouette.
                    const float tilt=float(int((leafHash>>8)%101)-50)*.020f;
                    const V3 baseAcross=normalize(cross(forward,{0,1,0})+V3{0,tilt,0});
                    for(uint32_t plane=0;plane<settings.leafPlanes;++plane){
                    const V3 across=plane==0?baseAcross:normalize(baseAcross*.38f+cross(forward,baseAcross)*.92f);
                    const V3 normal=normalize(cross(across,forward));
                    const float width=length*(.62f+.12f*float(leafHash%101)/100.f)*(settings.compactConifer&&settings.sprayPairs==3?1.35f:1.f);
                    const uint32_t base=uint32_t(out.vertices.size());
                    const uint32_t cell=uint32_t((leafHash>>20)%4);
                    const float u=float(cell%2)*.5f, v=float(cell/2)*.5f;
                    const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                        anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                    const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                    for(int k=0;k<4;++k) {
                        // Needle clusters shade as a volume rather than four
                        // unrelated paper faces. Normal maps remain shared PBR.
                        const V3 outward=normalize(V3{points[k].x,tree.heightCm*.12f,points[k].z});
                        const V3 n=settings.compactConifer?normalize(normal*.35f+outward*.65f):normal;
                        out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});
                    }
                    addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                    if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
                    }
                }
            }
        }
        if(settings.compactConifer&&settings.includeLeader) {
            // The terminal leader carries short ascending shoots too. Without
            // them the shrinking upper limbs leave a bare pole above the crown.
            const uint32_t shoots=settings.sprayPairs==1?4u:settings.sprayPairs==3?6u:12u;
            for(uint32_t i=0;i<shoots;++i) {
                const uint64_t h=detail::mix64(tree.seed^uint64_t(i+1)*7919);
                if(h%1000>=settings.leafCoveragePermille)continue;
                const float a=kTau*float(h%65536)/65536.f;
                const V3 forward=normalize(V3{std::cos(a)*.14f,1,std::sin(a)*.14f});
                const V3 across{std::sin(a),0,-std::cos(a)};
                const float startY=float(tree.heightCm)-130.f+80.f*float(i)/float(std::max(1u,shoots-1));
                const V3 anchor{0,startY,0};const float length=float(tree.heightCm)-startY+12;
                const float width=std::min(35.f,length*.48f);
                const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                    anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                const uint32_t cell=uint32_t(h%4),base=uint32_t(out.vertices.size());
                const float u=float(cell%2)*.5f,v=float(cell/2)*.5f;
                const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                const V3 normal=normalize(V3{std::cos(a),.4f,std::sin(a)});
                for(int k=0;k<4;++k)out.vertices.push_back({points[k].x,points[k].y,points[k].z,normal.x,normal.y,normal.z,uv[k][0],uv[k][1],1});
                addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
            }
        }
        return out;
    }
    for (const LeafCluster& leaf : tree.leaves) {
        if(leaf.variation%1000>=settings.leafCoveragePermille)continue;
        const V3 at{float(leaf.xCm), float(leaf.yCm), float(leaf.zCm)};
        const float r = float(leaf.radiusCm);
        if (r <= 0) continue;
        // Two crossed alpha-cutout cards per cluster. A later GPU material
        // controls alpha mask, wind and LOD blend; no textures are baked here.
        addLeafQuad(out, at, {1, 0, 0}, r, settings.doubleSidedLeaves);
        if (settings.leafPlanes == 2)
            addLeafQuad(out, at, {0, 0, 1}, r, settings.doubleSidedLeaves);
    }
    return out;
}

} // namespace aiverse::terraweave
