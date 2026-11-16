#pragma once
// voxel-world JSON: {"blocks":[{"name":"stone","breakTicks":30,"drop":"cobblestone","texture":1}, ...]}
// Air is id 0 and implicit. Ids follow list order (1, 2, ...); keep the order stable for saves.
// Keys: name, solid (true), replaceable (false), shape cube|cross|fluid|none, layer opaque|cutout|translucent|none,
// opacity 0..15 (15; 0 for non-opaque layers), emission 0..15, breakTicks, unbreakable, drop (name), dropsSelf,
// dropCount, texture (all faces) | textures [6] | {"side","top","bottom"}, fluid 1..15, fluidSpread 0..7,
// fluidTicks, infiniteSource, falls, growInto (name), growChance 0..4096 (per 4096 random ticks),
// color "#rrggbb" | {"side","top","bottom"} (untextured view colour per face), alpha 0..255 (view opacity).
// Errors: JsonError("blocks[2].opacity: 0..15").
#include "VoxelTypes.h"
#include "engine/data/Json.h"
namespace aiverse::lib::systems {
inline BlockRegistry readBlockRegistry(const Json& root){
    const auto& list=root.isArray()?root:root["blocks"];
    if(!list.isArray())throw JsonError("blocks: expected an array");
    BlockRegistry out;std::vector<std::pair<std::string,std::string>> drops,grows;
    for(std::size_t i=0;i<list.size();++i){
        const auto& j=list[i];auto at="blocks["+std::to_string(i)+"]";
        if(!j.isObject())throw JsonError(at+": expected an object");
        BlockDef d;d.name=j.getStr("name");if(d.name.empty())throw JsonError(at+".name: required");
        if(d.name=="air"){if(i!=0)throw JsonError(at+": air is implicit id 0");continue;}
        auto num=[&](const char* key,std::int64_t def,std::int64_t lo,std::int64_t hi){auto v=j.getInt(key,def);if(v<lo||v>hi)throw JsonError(at+"."+key+": "+std::to_string(lo)+".."+std::to_string(hi));return v;};
        auto shape=j.getStr("shape",j.has("fluid")?"fluid":"cube");
        if(shape=="cube")d.shape=BlockShape::Cube;else if(shape=="cross")d.shape=BlockShape::Cross;else if(shape=="fluid")d.shape=BlockShape::Fluid;else if(shape=="none")d.shape=BlockShape::None;else throw JsonError(at+".shape: cube|cross|fluid|none");
        auto layer=j.getStr("layer",d.shape==BlockShape::Cross?"cutout":d.shape==BlockShape::Fluid?"translucent":d.shape==BlockShape::None?"none":"opaque");
        if(layer=="opaque")d.layer=RenderLayer::Opaque;else if(layer=="cutout")d.layer=RenderLayer::Cutout;else if(layer=="translucent")d.layer=RenderLayer::Translucent;else if(layer=="none")d.layer=RenderLayer::None;else throw JsonError(at+".layer: opaque|cutout|translucent|none");
        bool opaqueCube=d.shape==BlockShape::Cube&&d.layer==RenderLayer::Opaque;
        d.solid=j.getBool("solid",d.shape==BlockShape::Cube);
        d.replaceable=j.getBool("replaceable",d.shape==BlockShape::Fluid||d.shape==BlockShape::Cross);
        d.opacity=static_cast<std::uint8_t>(num("opacity",opaqueCube?15:d.shape==BlockShape::Fluid?2:0,0,15));
        d.emission=static_cast<std::uint8_t>(num("emission",0,0,15));
        d.breakTicks=static_cast<std::uint32_t>(num("breakTicks",20,0,1<<20));d.unbreakable=j.getBool("unbreakable",false);
        d.dropsSelf=j.getBool("dropsSelf",!j.has("drop"));d.dropCount=static_cast<std::uint32_t>(num("dropCount",1,0,64));
        if(j.has("drop"))drops.push_back({d.name,j.getStr("drop")});
        if(j.has("fluid")){d.fluid=static_cast<std::uint8_t>(num("fluid",1,1,15));d.fluidSpread=static_cast<std::uint8_t>(num("fluidSpread",7,0,7));
            d.fluidTicks=static_cast<std::uint32_t>(num("fluidTicks",5,1,1200));d.infiniteSource=j.getBool("infiniteSource",false);d.solid=j.getBool("solid",false);}
        d.falls=j.getBool("falls",false);
        if(j.has("growInto"))grows.push_back({d.name,j.getStr("growInto")});
        d.growChance=static_cast<std::uint32_t>(num("growChance",0,0,4096));
        const auto& t=j["textures"];
        if(t.isArray()){if(t.size()!=6)throw JsonError(at+".textures: 6 layers -X +X -Y +Y -Z +Z");for(std::size_t f=0;f<6;++f){auto v=t[f].integer(-1);if(v<0||v>65535)throw JsonError(at+".textures: 0..65535");d.textures[f]=static_cast<std::uint16_t>(v);}}
        else if(t.isObject()){auto side=t.getInt("side",0),top=t.getInt("top",side),bottom=t.getInt("bottom",side);
            for(auto v:{side,top,bottom})if(v<0||v>65535)throw JsonError(at+".textures: 0..65535");
            d.textures={static_cast<std::uint16_t>(side),static_cast<std::uint16_t>(side),static_cast<std::uint16_t>(bottom),static_cast<std::uint16_t>(top),static_cast<std::uint16_t>(side),static_cast<std::uint16_t>(side)};}
        else{auto v=num("texture",0,0,65535);d.textures.fill(static_cast<std::uint16_t>(v));}
        auto hex=[&](const Json& v,std::uint32_t def)->std::uint32_t{if(v.isNull())return def;auto t=v.str();
            if(t.size()!=7||t[0]!='#')throw JsonError(at+".color: \"#rrggbb\"");std::uint32_t c=0;
            for(std::size_t k=1;k<7;++k){char ch=t[k];int n=ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:ch>='A'&&ch<='F'?ch-'A'+10:-1;if(n<0)throw JsonError(at+".color: \"#rrggbb\"");c=c*16+static_cast<std::uint32_t>(n);}
            return c;};
        const auto& col=j["color"];
        if(col.isObject()){auto side=hex(col["side"],0x808080),top=hex(col["top"],side),bottom=hex(col["bottom"],side);d.colors={side,side,bottom,top,side,side};}
        else d.colors.fill(hex(col,0x808080));
        d.alpha=static_cast<std::uint8_t>(num("alpha",d.layer==RenderLayer::Translucent?180:255,0,255));
        if(!out.add(d))throw JsonError(at+".name: duplicate '"+d.name+"' or more than "+std::to_string(kMaxBlockTypes)+" blocks");
    }
    auto resolve=[&](const std::vector<std::pair<std::string,std::string>>& links,bool drop){
        for(const auto& [from,to]:links){auto a=out.find(from);auto b=out.find(to);if(!b)throw JsonError("blocks."+from+(drop?".drop":".growInto")+": unknown block '"+to+"'");
            auto& d=out.defs[*a];if(drop)d.drop=*b;else d.growInto=*b;}};
    resolve(drops,true);resolve(grows,false);
    if(auto e=validateRegistry(out))throw JsonError(*e);
    return out;
}
}
