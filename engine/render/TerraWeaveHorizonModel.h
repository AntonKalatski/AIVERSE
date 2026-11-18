// TerraWeave Horizon presentation: a SINGLE standard AIVERSE Model with
// one coarse terrain primitive and one forest-cluster primitive. The latter
// shares ONE packed RGBA8 atlas for thousands of crowns, not per-tree draws.
// Rendering, shadows and buffer upload remain owned by Renderer3D/gfx::Device.
#pragma once
#include "engine/world/TerraWeaveHorizon.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

inline std::unique_ptr<render::Model> makeHorizonModel(const HorizonBake& b) {
    if (b.atlasWidth == 0 || b.atlasWidth != b.atlasHeight ||
        b.atlasRgba.size() != size_t(b.atlasWidth)*b.atlasHeight*4)
        throw std::invalid_argument("TerraWeave Horizon invalid RGBA atlas");
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/horizon";
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false; // required for safe mutable terrain vertex ranges

    render::Material ground;
    ground.name="TerraWeave Horizon Terrain";
    ground.shading=render::ShadingModel::Pbr;
    ground.baseColor={1,1,1,1};
    ground.roughness=1.0f;
    ground.castShadow=false;
    model->materials.push_back(ground);

    render::Material forest;
    forest.name="TerraWeave Horizon Forest Cluster";
    forest.shading=render::ShadingModel::Unlit;
    forest.baseColor={1,1,1,1};
    forest.baseTex=0;
    forest.alpha=render::AlphaMode::Mask;
    forest.alphaCutoff=0.5f;
    forest.doubleSided=true;
    forest.castShadow=false;
    forest.receiveShadow=false;
    forest.roughness=1.0f;
    model->materials.push_back(forest);

    render::Image atlas;
    atlas.name="terraweave/horizon_clusters_rgba8";
    atlas.width=static_cast<int>(b.atlasWidth);
    atlas.height=static_cast<int>(b.atlasHeight);
    atlas.repeat=false;
    atlas.nearest=false;
    atlas.rgba=b.atlasRgba;
    model->images.push_back(std::move(atlas));

    render::MeshDef mesh;
    mesh.name="TerraWeave Horizon Combined";
    if (!b.terrainVertices.empty()) {
        const uint32_t n=b.terrainResolution;
        if (b.terrainVertices.size()!=size_t(n+1)*(n+1))
            throw std::invalid_argument("TerraWeave Horizon terrain samples");
        const uint32_t start=static_cast<uint32_t>(model->vertices.size());
        for (uint32_t z=0;z<=n;++z) {
            for (uint32_t x=0;x<=n;++x) {
                const auto& v=b.terrainVertices[size_t(z)*(n+1)+x];
                // Normals are authored from the same globally anchored
                // height stencil in every region, including outer borders.
                // Never use a one-sided normal at the local patch boundary.
                const auto normal=render::normalize(render::float3{
                    float(v.slopeXPermille)*0.001f,1.0f,
                    float(v.slopeZPermille)*0.001f
                });
                render::ModelVertex out;
                out.pos={
                    float(v.xCm-b.originXcm)*0.01f,
                    float(v.heightCm)*0.01f,
                    float(v.zCm-b.originZcm)*0.01f
                };
                out.nrm=normal;
                out.uv={
                    float(v.xCm-b.originXcm)*0.00002f,
                    float(v.zCm-b.originZcm)*0.00002f
                };
                out.color=v.colorRgba;
                model->vertices.push_back(out);
            }
        }
        const uint32_t begin=static_cast<uint32_t>(model->indices.size());
        for (uint32_t z=0;z<n;++z)
            for (uint32_t x=0;x<n;++x) {
                const size_t idx=size_t(z)*(n+1)+x;
                const auto& a=b.terrainVertices[idx];
                const auto& d=b.terrainVertices[idx+(n+1)+1];
                const int64_t cx=(a.xCm+d.xCm)/2-b.cameraXcm;
                const int64_t cz=(a.zCm+d.zCm)/2-b.cameraZcm;
                if (cx*cx+cz*cz<
                    int64_t(b.terrainBeginCm)*b.terrainBeginCm)
                    continue; // near authored terrain remains the owner
                const uint32_t i=start+static_cast<uint32_t>(idx);
                const uint32_t stride=n+1;
                model->indices.insert(model->indices.end(),{
                    i,i+stride,i+1,
                    i+1,i+stride,i+stride+1
                });
            }
        // An optional lightweight ring extends the outer silhouette
        // DOWNWARD without altering the true sampled top-edge heights.
        // It hides sub-mesh cracks between a coarse far field and neighboring
        // landscape while preserving deterministic world-position edges.
        if(b.terrainSkirtDepthCm>0){
            const uint32_t lowerFirst=
                static_cast<uint32_t>(model->vertices.size());
            const auto rimIndex=[n](uint32_t side,uint32_t i)->uint32_t{
                switch(side){
                    case 0:return i;                       // north, eastward
                    case 1:return i*(n+1)+n;              // east, southward
                    case 2:return n*(n+1)+(n-i);          // south, westward
                    default:return (n-i)*(n+1);           // west, northward
                }
            };
            const auto dimColor=[](uint32_t color)->uint32_t{
                const uint32_t red=(color&255u)*3u/4u;
                const uint32_t green=((color>>8)&255u)*3u/4u;
                const uint32_t blue=((color>>16)&255u)*3u/4u;
                return (color&0xff000000u)|(blue<<16)|(green<<8)|red;
            };
            for(uint32_t side=0;side<4;++side){
                for(uint32_t i=0;i<=n;++i){
                    auto bottom=model->vertices[start+rimIndex(side,i)];
                    bottom.pos.y-=float(b.terrainSkirtDepthCm)*0.01f;
                    bottom.nrm=render::normalize(
                        render::float3{side==1?1.0f:side==3?-1.0f:0.0f,
                                       0.25f,
                                       side==2?1.0f:side==0?-1.0f:0.0f});
                    bottom.color=dimColor(bottom.color);
                    model->vertices.push_back(bottom);
                }
                for(uint32_t i=0;i<n;++i) {
                    const uint32_t topA=start+rimIndex(side,i);
                    const uint32_t topB=start+rimIndex(side,i+1);
                    const uint32_t lowA=lowerFirst+side*(n+1)+i;
                    const uint32_t lowB=lowA+1;
                    model->indices.insert(model->indices.end(),{
                        topA,topB,lowA,lowA,topB,lowB
                    });
                }
            }
        }
        if (model->indices.size()>begin) {
            render::Primitive primitive;
            primitive.material=0;
            primitive.firstVertex=start;
            primitive.vertexCount=static_cast<uint32_t>(
                model->vertices.size()-start);
            primitive.firstIndex=begin;
            primitive.indexCount=static_cast<uint32_t>(model->indices.size())-begin;
            mesh.primitives.push_back(primitive);
        }
    }

    const uint32_t forestVertexStart=static_cast<uint32_t>(model->vertices.size());
    const uint32_t forestIndexStart=static_cast<uint32_t>(model->indices.size());
    for (const HorizonCluster& cluster:b.clusters) {
        const float x=float(cluster.centerXcm-b.originXcm)*0.01f;
        const float z=float(cluster.centerZcm-b.originZcm)*0.01f;
        const float base=float(cluster.bottomCm)*0.01f;
        const float top=float(cluster.topCm)*0.01f;
        const float half=float(cluster.tileSizeCm)*0.005f;
        for(int orientation=0;orientation<2;++orientation) {
            // One atlas tile per cluster in economy mode. Higher quality
            // may bake an independent 90-degree silhouette, but the same
            // two crossed cards and single material/texture are retained.
            const uint32_t atlasX=orientation==0 ?
                cluster.atlasX : cluster.atlasSideX;
            const uint32_t atlasY=orientation==0 ?
                cluster.atlasY : cluster.atlasSideY;
            const float u0=(float(atlasX)+0.5f)/float(b.atlasWidth);
            const float u1=(float(atlasX+cluster.atlasSize)-0.5f)/float(b.atlasWidth);
            const float v0=(float(atlasY)+0.5f)/float(b.atlasHeight);
            const float v1=(float(atlasY+cluster.atlasSize)-0.5f)/float(b.atlasHeight);
            const float dx=orientation==0?half:0.0f;
            const float dz=orientation==0?0.0f:half;
            const uint32_t first=static_cast<uint32_t>(model->vertices.size());
            const float nx=orientation==0?0:1, nz=orientation==0?1:0;
            const render::float3 pos[4] = {
                {x-dx,base,z-dz}, {x+dx,base,z+dz},
                {x+dx,top,z+dz}, {x-dx,top,z-dz}
            };
            const render::float2 uv[4] = {
                {u0,v1},{u1,v1},{u1,v0},{u0,v0}
            };
            for (int i=0;i<4;++i) {
                render::ModelVertex v;
                v.pos=pos[i];v.nrm={nx,0,nz};v.uv=uv[i];
                model->vertices.push_back(v);
            }
            model->indices.insert(model->indices.end(),{
                first,first+1,first+2,first,first+2,first+3
            });
        }
    }
    if (model->indices.size()>forestIndexStart) {
        render::Primitive p;
        p.material=1;
        p.firstVertex=forestVertexStart;
        p.vertexCount=static_cast<uint32_t>(model->vertices.size())-forestVertexStart;
        p.firstIndex=forestIndexStart;
        p.indexCount=static_cast<uint32_t>(model->indices.size())-forestIndexStart;
        mesh.primitives.push_back(p);
    }
    if(mesh.primitives.empty())return {};
    model->meshes.push_back(std::move(mesh));
    render::Node root;
    root.name="TerraWeave Horizon Combined";
    root.mesh=0;
    model->nodes.push_back(std::move(root));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
