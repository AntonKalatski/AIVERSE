#pragma once
#include "apps/editor/Devices.h"
#include "engine/data/Json.h"
#include "engine/platform/Platform.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
struct StudioGameDevice {
    std::string id="none";
    bool landscape=true, safeArea=true;
    const editor::DeviceProfile& profile() const {
        const auto* p=editor::findDevice(id);
        if (!p) throw std::runtime_error("Unknown Game device profile");
        return *p;
    }
    std::pair<float,float> size(float width,float height) const {
        const auto& p=profile();
        if (!p.w) return {width,height};
        float w=p.w,h=p.h;
        if ((w<h)==landscape) std::swap(w,h);
        return {w,h};
    }
    platform::Insets insets() const {
        if (!safeArea) return {};
        const auto v=editor::deviceInsets(profile(),landscape);
        return {v.left,v.top,v.right,v.bottom};
    }
    Json json() const {
        Json out=Json::object();const auto wh=size(0,0);
        out.set("id",Json::string(id));out.set("landscape",Json::fromBool(landscape));
        out.set("safeArea",Json::fromBool(safeArea));out.set("width",Json::number(double(wh.first)));
        out.set("height",Json::number(double(wh.second)));out.set("touch",Json::fromBool(profile().touch));
        return out;
    }
    static StudioGameDevice parse(const Json& args) {
        if (!args["id"].isString() || args["landscape"].type()!=Json::Type::Bool || args["safeArea"].type()!=Json::Type::Bool)
            throw std::runtime_error("Device requires id, landscape and safeArea");
        StudioGameDevice d;d.id=args.getStr("id");d.landscape=args["landscape"].boolean();
        d.safeArea=args["safeArea"].boolean();(void)d.profile();return d;
    }
};
inline Json studioGameDevices() {
    Json out=Json::array();
    for (const auto& p:editor::kDevices) {
        Json v=Json::object();v.set("id",Json::string(p.id));
        v.set("name",Json::string(p.name));v.set("group",Json::string(p.group));out.push(std::move(v));
    }
    return out;
}
}
