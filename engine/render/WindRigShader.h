// GPU side of hierarchical wind masks (WindRig.h). One shared, first-party
// material ShaderGraph program for EVERY rigged object: graph texture 0 = the
// prototype's rig table, graph texture 1 = the global mask atlas. Uses the
// renderer's vertexOffset injection and SG_TEXEL<i> exact texel fetches; no
// new vertex streams, RHI buffers or per-object shader variants.
//
// The code mirrors wind::PackedWindRig::offset() line by line; keep both in
// sync (tests/wind_rig_test.cpp checks the CPU side and the packing).
#pragma once
#include "engine/render/WindRig.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/render/Math3D.h"
#include <memory>

namespace aiverse::render::wind {

namespace shader_detail {
// %VEC%, %VEC3%, %VEC2%, %MAT3%, %INT% are replaced per language.
inline constexpr const char* kBody = R"(
%VEC% wr_h=SG_TEXEL0(0,0);
float wr_count=floor(wr_h.x*255.0+0.5)+floor(wr_h.y*255.0+0.5)*256.0;
float wr_unit=exp2(floor(wr_h.z*255.0+0.5)-20.0);
float wr_node=floor(sg_color.x*255.0+0.5)+floor(sg_color.y*255.0+0.5)*256.0;
float wr_flutter=sg_color.z;
%VEC3% wr_W=%VEC3%(I.anim.x,0.0,I.anim.y);
float wr_strength=length(wr_W);
%VEC3% wr_q=sg_object;
%VEC3% wr_off=%VEC3%(0.0,0.0,0.0);
for(int wr_l=0;wr_l<4;++wr_l){
  if(wr_node>=wr_count)break;
  int wr_b=1+int(wr_node)*4;
  %VEC% wr_n0=SG_TEXEL0(wr_b%64,wr_b/64);
  %VEC% wr_n1=SG_TEXEL0((wr_b+1)%64,(wr_b+1)/64);
  %VEC% wr_n2=SG_TEXEL0((wr_b+2)%64,(wr_b+2)/64);
  %VEC% wr_n3=SG_TEXEL0((wr_b+3)%64,(wr_b+3)/64);
  %VEC% wr_b0=floor(wr_n0*255.0+0.5);
  %VEC% wr_b1=floor(wr_n1*255.0+0.5);
  %VEC% wr_b2=floor(wr_n2*255.0+0.5);
  float wr_px=wr_b0.x+wr_b0.y*256.0; wr_px-=wr_px>=32768.0?65536.0:0.0;
  float wr_py=wr_b0.z+wr_b0.w*256.0; wr_py-=wr_py>=32768.0?65536.0:0.0;
  float wr_pz=wr_b1.x+wr_b1.y*256.0; wr_pz-=wr_pz>=32768.0?65536.0:0.0;
  %VEC3% wr_pivot=%VEC3%(wr_px,wr_py,wr_pz)*wr_unit;
  float wr_len=max((wr_b1.z+wr_b1.w*256.0)*wr_unit,0.01);
  %VEC2% wr_o=wr_n2.xy*2.0-1.0;
  %VEC3% wr_axis=%VEC3%(wr_o.x,1.0-abs(wr_o.x)-abs(wr_o.y),wr_o.y);
  if(wr_axis.y<0.0){
    %VEC2% wr_s=%VEC2%(wr_axis.x>=0.0?1.0:-1.0,wr_axis.z>=0.0?1.0:-1.0);
    wr_axis.xz=(1.0-abs(wr_axis.zx))*wr_s;
  }
  wr_axis=normalize(wr_axis);
  float wr_parent=wr_b2.z+wr_b2.w*256.0;
  int wr_mask=int(floor(wr_n3.x*255.0+0.5));
  float wr_stiff=wr_n3.y, wr_phase0=wr_n3.z, wr_flex=wr_n3.w*2.0;
  float wr_t=clamp(dot(wr_q-wr_pivot,wr_axis)/wr_len,0.0,1.0);
  float wr_f=wr_t*31.0;
  int wr_i0=int(floor(wr_f)); int wr_i1=min(wr_i0+1,31);
  int wr_a0=wr_mask*8+wr_i0/4; int wr_a1=wr_mask*8+wr_i1/4;
  %VEC% wr_m0=SG_TEXEL1(wr_a0%64,wr_a0/64);
  %VEC% wr_m1=SG_TEXEL1(wr_a1%64,wr_a1/64);
  float wr_m=mix(wr_m0[wr_i0&3],wr_m1[wr_i1&3],wr_f-float(wr_i0));
  %VEC3% wr_perp=wr_W-wr_axis*dot(wr_W,wr_axis);
  float wr_ph=I.anim.z*(0.6+1.4*wr_stiff)+wr_phase0*6.2831853;
  float wr_wave=0.62+0.38*sin(wr_ph)+I.anim.w*0.25*sin(wr_ph*2.13+1.7);
  %VEC3% wr_d=wr_perp*(wr_m*wr_flex*wr_wave);
  float wr_side=1.0-abs(wr_axis.y);
  wr_d.y+=wr_strength*wr_m*wr_flex*0.25*wr_side*sin(wr_ph*1.31+0.9);
  wr_d-=wr_axis*(dot(wr_d,wr_d)*0.5/wr_len);
  wr_off+=wr_d;
  wr_q=wr_pivot;
  wr_node=wr_parent;
}
%VEC3% wr_on=normalize(transpose(%MAT3%(sg_c0,sg_c1,sg_c2))*sg_normal+%VEC3%(0.0,1e-6,0.0));
float wr_fl=wr_flutter*wr_strength*0.08*sin(I.anim.z*6.7+dot(sg_object,%VEC3%(3.1,1.7,2.3)));
%VEC% wr_offset=%VEC%(wr_off+wr_on*wr_fl,0.0);
)";

inline std::string specialize(bool metal) {
    std::string s = kBody;
    const auto rep = [&](const std::string& from, const std::string& to) {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
            s.replace(at, from.size(), to);
    };
    rep("%VEC3%", metal ? "float3" : "vec3");
    rep("%VEC2%", metal ? "float2" : "vec2");
    rep("%MAT3%", metal ? "float3x3" : "mat3");
    rep("%VEC%", metal ? "float4" : "vec4");
    return s;
}
} // namespace shader_detail

// Shared program for all rigged materials. Fragment colour is unchanged
// (sg_input); vertex colours carry wind data, so set vertexColors=false.
inline const std::shared_ptr<const graph::Program>& windRigGraph() {
    static const auto program = [] {
        auto p = std::make_shared<graph::Program>();
        p->domain = "material";
        p->minQuality = 0;
        p->textures = {"aiverse:wind-rig", "aiverse:wind-masks"};
        p->outputs["color"] = "sg_input";
        p->outputs["vertexOffset"] = "wr_offset";
        p->vertexGlsl = shader_detail::specialize(false);
        p->vertexMsl = shader_detail::specialize(true);
        return std::shared_ptr<const graph::Program>(std::move(p));
    }();
    return program;
}

// Conservative per-draw bound for culling (ModelDraw::maxVertexOffsetMetres):
// |wind| * max flex (2) * wave (1.25) per level, 4 levels, + flutter.
inline float windRigMaxOffsetMetres(const WindDrawParams& w) {
    const float s = std::sqrt(w.windX * w.windX + w.windZ * w.windZ);
    return s * (2.f * 1.25f * 1.25f * float(kMaxLevels) + 0.08f) + 0.01f;
}

inline render::float4 windRigVertexParameters(const WindDrawParams& w) {
    return {w.windX, w.windZ, w.phase, w.gust};
}

} // namespace aiverse::render::wind
