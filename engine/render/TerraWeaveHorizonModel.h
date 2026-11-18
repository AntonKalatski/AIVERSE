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
        for (const auto& v:b.terrainVertices) {
            render::ModelVertex out;
            out.pos={
                float(v.xCm-b.originXcm)*0.01f,
                float(v.heightCm)*0.01f,
                float(v.zCm-b.originZcm)*0.01f
            };
            out.nrm={0,1,0};
            out.uv={
                float(v.xCm-b.originXcm)*0.00002f,
                float(v.zCm-b.originZcm)*0.00002f
            };
            out.color=v.colorRgba;
            model->vertices.push_back(out);
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
        if (model->indices.size()>begin) {
            render::Primitive primitive;
            primitive.material=0;
            primitive.firstVertex=start;
            primitive.vertexCount=static_cast<uint32_t>(b.terrainVertices.size());
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
        const float u0=(float(cluster.atlasX)+0.5f)/float(b.atlasWidth);
        const float u1=(float(cluster.atlasX+cluster.atlasSize)-0.5f)/float(b.atlasWidth);
        const float v0=(float(cluster.atlasY)+0.5f)/float(b.atlasHeight);
        const float v1=(float(cluster.atlasY+cluster.atlasSize)-0.5f)/float(b.atlasHeight);
        for(int orientation=0;orientation<2;++orientation) {
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
