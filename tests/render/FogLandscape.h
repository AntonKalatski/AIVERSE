#pragma once
// Asset-free lighting/occlusion reference fixture; intentionally simple geometry.
namespace aiverse::test {
inline void initFogLandscape(HdrScene& s,gfx::Device& d,const std::string& which){
    s.fogLandscape=true;const bool valley=which.starts_with("valley");
    auto height=[&](float x,float z){return valley?.035f*x*x+1.2f*std::sin(z*.16f)*std::sin(x*.13f):.5f*std::sin(x*.21f)+.4f*std::cos(z*.18f);};
    MeshBuilder terrain,trees,rocks;
    const int nx=70,nz=80;
    for(int z=0;z<=nz;++z)for(int x=0;x<=nx;++x){
        float wx=float(x)-35,wz=float(z)-55;
        float3 n=normalize(float3{height(wx-.1f,wz)-height(wx+.1f,wz),.2f,height(wx,wz-.1f)-height(wx,wz+.1f)});
        terrain.addVertex({wx,height(wx,wz),wz},n,0xffffffffu);
    }
    for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){uint32_t a=z*(nx+1)+x,b=a+1,c=a+nx+1,e=c+1;terrain.addTriangle(a,c,b);terrain.addTriangle(b,c,e);}
    uint32_t rng=17;auto random=[&](){rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.f;};
    for(int i=0;i<140;++i){
        float x=(random()-.5f)*64,z=random()*72-52;
        if(std::abs(x)<4&&z> -24)continue;
        float y=height(x,z),h=3+random()*5,r=.6f+h*.12f;
        trees.cylinder({x,y,z},.12f,h,6);
        for(int layer=0;layer<3;++layer)trees.cone({x,y+h*(.18f+.21f*layer),z},r*(1.f-.18f*layer),h*.57f,9);
    }
    for(int i=0;i<24;++i){float x=(random()-.5f)*58,z=random()*70-48,r=.8f+random()*1.3f;rocks.sphere({x,height(x,z)+r*.2f,z},r,5,8);}
    // Tall silhouettes reveal fog depth and foreground preservation.
    for(int i=0;i<6;++i){float x=float(i%2?1:-1)*(5+i*.8f),z=8-i*6.f;rocks.cylinder({x,height(x,z),z},.8f,3+i*.4f,6,true,.35f);}
    s.fogTerrain=terrain.upload(d);s.fogTrees=trees.upload(d);s.fogRocks=rocks.upload(d);
    s.camera.target={0,valley?5.f:1.4f,-13};s.camera.distance=valley?42.f:28.f;s.camera.pitch=valley?.25f:.11f;s.camera.yaw=.16f;
    s.renderer.lighting.sunDir={-.4f,.65f,-.6f};s.renderer.lighting.sunColor={.78f,.86f,1};s.renderer.lighting.sunIntensity=valley?.55f:.3f;
    s.renderer.lighting.skyColor={.32f,.4f,.52f};s.renderer.lighting.groundColor={.15f,.19f,.16f};
    s.post.localFog().clear();LocalFogSettings settings;settings.enabled=!which.ends_with("-off");settings.ambient=valley?.16f:.1f;settings.anisotropy=.15f;s.post.setLocalFog(settings);
    FogVolume bank;bank.id=1;bank.region.shape=FogShape::Obb;bank.region.center={0,valley?5.f:1.2f,-14};
    bank.region.halfExtents={32,valley?9.f:3.f,43};bank.region.feather=valley?3.f:.5f;
    bank.density=valley?.07f:.045f;bank.color={.72f,.8f,.88f};bank.structure.amount=1;
    bank.structure.scale=valley?float3{12,3.5f,10}:float3{8,.65f,5};bank.structure.coverage=valley?.62f:.58f;
    bank.structure.softness=.22f;bank.structure.heightBase=valley?1.f:.2f;bank.structure.heightFalloff=valley?.13f:1.3f;
    bank.structure.wind={.6f,0,.2f};
    if(!s.post.localFog().upsert(bank)){s.failed=true;return;}
    s.post.setLocalFogTime(which.ends_with("-wind")?8:0);
    if(which.ends_with("-reveal")){FogReveal r;r.id=1;r.region=bank.region;r.region.feather=0;s.post.localFog().upsert(r);}
}
inline void drawFogLandscape(HdrScene& s){
    s.renderer.drawMesh(s.fogTerrain,{}, {.12f,.16f,.11f,1},kDrawUnlit);
    s.renderer.drawMesh(s.fogTrees,{}, {.055f,.12f,.085f,1},kDrawUnlit);
    s.renderer.drawMesh(s.fogRocks,{}, {.2f,.23f,.21f,1},kDrawUnlit);
}
}
