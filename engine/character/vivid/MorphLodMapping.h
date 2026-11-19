// VIVID versioned, non-destructive facial LOD surface mapping sidecar.
#pragma once
#include "MorphLodSurface.h"
#include "engine/data/Json.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
struct MorphLodMapping {
    static constexpr std::uint32_t kVersion=1;
    std::uint32_t sourceVertices=0;
    std::uint32_t sourceTriangles=0;
    std::vector<LodSurfaceSample> samples;

    void validate() const {
        if(!sourceVertices || sourceVertices>16'000'000 ||
           !sourceTriangles || sourceTriangles>5'000'000 ||
           samples.empty()||samples.size()>16'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid dimensions");
        for(const auto& sample:samples) {
            if(sample.triangle>=sourceTriangles ||
               !std::isfinite(sample.bary.x)||
               !std::isfinite(sample.bary.y)||
               !std::isfinite(sample.bary.z)||
               !std::isfinite(sample.distanceSquared)||
               sample.distanceSquared<0)
                throw std::invalid_argument("VIVID LOD mapping: invalid projection sample");
            const float total=sample.bary.x+sample.bary.y+sample.bary.z;
            if(std::fabs(total-1)>1.e-3f||
               sample.bary.x< -1.e-4f||sample.bary.y< -1.e-4f||
               sample.bary.z< -1.e-4f)
                throw std::invalid_argument("VIVID LOD mapping: invalid barycentric weights");
        }
    }
    Json toJson() const {
        validate();
        Json root=Json::object();
        root.set("version",Json::number(std::int64_t(kVersion)));
        root.set("sourceVertices",Json::number(std::int64_t(sourceVertices)));
        root.set("sourceTriangles",Json::number(std::int64_t(sourceTriangles)));
        Json records=Json::array();
        for(const auto& sample:samples) {
            Json record=Json::object();
            record.set("triangle",Json::number(std::int64_t(sample.triangle)));
            Json weights=Json::array();
            for(float value:{sample.bary.x,sample.bary.y,sample.bary.z})
                weights.push(Json::number(double(value),9));
            record.set("bary",std::move(weights));
            record.set("distanceSquared",Json::number(double(sample.distanceSquared),9));
            records.push(std::move(record));
        }
        root.set("samples",std::move(records));
        return root;
    }
    static MorphLodMapping fromJson(const Json& root) {
        if(!root.isObject() || !root["version"].isNumber() ||
           root["version"].integer(-1)!=kVersion ||
           !root["sourceVertices"].isNumber()||
           !root["sourceTriangles"].isNumber()||
           !root["samples"].isArray()||
           root["samples"].size()>16'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid schema/version");
        const auto vertices=root["sourceVertices"].integer(-1);
        const auto triangles=root["sourceTriangles"].integer(-1);
        if(vertices<=0||vertices>16'000'000||
           triangles<=0||triangles>5'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid source dimensions");
        MorphLodMapping result;
        result.sourceVertices=std::uint32_t(vertices);
        result.sourceTriangles=std::uint32_t(triangles);
        result.samples.reserve(root["samples"].size());
        for(const auto& record:root["samples"].items()) {
            if(!record.isObject() || !record["triangle"].isNumber() ||
               !record["bary"].isArray() || record["bary"].size()!=3 ||
               !record["distanceSquared"].isNumber())
                throw std::invalid_argument("VIVID LOD mapping: malformed sample");
            const auto id=record["triangle"].integer(-1);
            const auto& weights=record["bary"];
            if(id<0||id>=triangles ||
               !weights[0].isNumber()||!weights[1].isNumber()||
               !weights[2].isNumber())
                throw std::invalid_argument("VIVID LOD mapping: invalid sample values");
            LodSurfaceSample sample;
            sample.triangle=std::uint32_t(id);
            sample.bary={
                float(weights[0].toDouble()),
                float(weights[1].toDouble()),
                float(weights[2].toDouble())};
            sample.distanceSquared=float(record["distanceSquared"].toDouble());
            result.samples.push_back(sample);
        }
        result.validate();
        return result;
    }
    SparseMorphLibrary transfer(const SparseMorphLibrary& morphs,
                                std::span<const std::uint32_t> indices) const {
        validate();
        if(morphs.vertices()!=sourceVertices ||
           indices.size()/3!=sourceTriangles ||
           indices.size()%3)
            throw std::invalid_argument("VIVID LOD mapping: source topology mismatch");
        return MorphLodSurface::transfer(morphs,indices,samples);
    }
};
} // namespace aiverse::vivid
