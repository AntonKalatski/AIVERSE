#pragma once
// voxel-gen JSON: {"voxelGen":{"seed":7,"seaLevel":62,"baseHeight":64,"heightAmplitude":24,"heightScale":128,
//   "octaves":5,"bedrockY":-64,"stone":"stone","water":"water","bedrock":"bedrock",
//   "caves":{"scale":24,"minY":-60,"maxY":120,"threshold":40000},"biomeScale":512,"tree":{"minHeight":4,"maxHeight":6},
//   "biomes":[{"name":"plains","upTo":600,"surface":"grass","filler":"dirt","shore":"sand","fillerDepth":3,
//              "trees":2,"log":"log","leaves":"leaves","plants":20,"plant":"tallgrass","heightOffset":0}, ...],
//   "ores":[{"block":"coal_ore","replaces":"stone","minY":0,"maxY":128,"veins":10,"size":10}]}}
// Block names resolve through the registry. Errors: JsonError("voxelGen.biomes[1].surface: unknown block 'x'").
#include "VoxelGen.h"
#include "engine/data/Json.h"
namespace aiverse::lib::systems {
inline VoxelGenConfig readVoxelGen(const Json& root,const BlockRegistry& r){
    const auto& j=root.has("voxelGen")?root["voxelGen"]:root;if(!j.isObject())throw JsonError("voxelGen: expected an object");
    VoxelGenConfig c;
    auto block=[&](const Json& o,const char* key,const std::string& at,bool required)->BlockId{
        if(!o.has(key)){if(required)throw JsonError(at+"."+key+": required");return kAir;}
        auto name=o.getStr(key);auto id=r.find(name);if(!id)throw JsonError(at+"."+key+": unknown block '"+name+"'");return *id;};
    auto num=[&](const Json& o,const char* key,std::int64_t def,std::int64_t lo,std::int64_t hi,const std::string& at){auto v=o.getInt(key,def);if(v<lo||v>hi)throw JsonError(at+"."+key+": "+std::to_string(lo)+".."+std::to_string(hi));return v;};
    const std::string at="voxelGen";
    auto seed=j.getInt("seed",1);c.seed=static_cast<std::uint64_t>(seed);
    c.seaLevel=static_cast<std::int32_t>(num(j,"seaLevel",62,-4096,4096,at));c.baseHeight=static_cast<std::int32_t>(num(j,"baseHeight",64,-4096,4096,at));
    c.heightAmplitude=static_cast<std::int32_t>(num(j,"heightAmplitude",24,0,1024,at));c.heightScale=static_cast<std::int32_t>(num(j,"heightScale",128,1,1<<16,at));
    c.octaves=static_cast<std::uint32_t>(num(j,"octaves",5,1,8,at));c.bedrockY=static_cast<std::int32_t>(num(j,"bedrockY",-64,-(1<<20),1<<20,at));
    c.stone=block(j,"stone",at,true);c.water=block(j,"water",at,false);c.bedrock=block(j,"bedrock",at,false);
    if(j.has("caves")){const auto& cv=j["caves"];if(cv.type()==Json::Type::Bool){c.caves=cv.boolean();}else{c.caves=cv.getBool("enabled",true);
        c.caveScale=static_cast<std::int32_t>(num(cv,"scale",24,1,1024,at+".caves"));c.caveMinY=static_cast<std::int32_t>(num(cv,"minY",-60,-(1<<20),1<<20,at+".caves"));
        c.caveMaxY=static_cast<std::int32_t>(num(cv,"maxY",120,-(1<<20),1<<20,at+".caves"));c.caveThreshold=static_cast<std::uint32_t>(num(cv,"threshold",40000,0,65535,at+".caves"));}}
    c.biomeScale=static_cast<std::int32_t>(num(j,"biomeScale",512,1,1<<16,at));
    if(j.has("tree")){c.treeMinHeight=static_cast<std::uint32_t>(num(j["tree"],"minHeight",4,1,12,at+".tree"));c.treeMaxHeight=static_cast<std::uint32_t>(num(j["tree"],"maxHeight",6,1,12,at+".tree"));}
    const auto& bs=j["biomes"];if(!bs.isArray()||bs.size()==0)throw JsonError(at+".biomes: at least one biome");
    for(std::size_t i=0;i<bs.size();++i){const auto& b=bs[i];auto bat=at+".biomes["+std::to_string(i)+"]";VoxelBiome v;v.name=b.getStr("name","biome"+std::to_string(i));
        v.upTo=static_cast<std::uint32_t>(num(b,"upTo",i+1==bs.size()?1000:0,1,1000,bat));v.surface=block(b,"surface",bat,true);v.filler=block(b,"filler",bat,true);v.shore=block(b,"shore",bat,false);
        v.fillerDepth=static_cast<std::uint32_t>(num(b,"fillerDepth",3,0,64,bat));v.treesPerChunk=static_cast<std::uint32_t>(num(b,"trees",0,0,32,bat));
        v.log=block(b,"log",bat,v.treesPerChunk>0);v.leaves=block(b,"leaves",bat,false);v.plantsPerChunk=static_cast<std::uint32_t>(num(b,"plants",0,0,64,bat));v.plant=block(b,"plant",bat,v.plantsPerChunk>0);
        v.heightOffset=static_cast<std::int32_t>(num(b,"heightOffset",0,-1024,1024,bat));c.biomes.push_back(v);}
    const auto& os=j["ores"];
    for(std::size_t i=0;i<os.size();++i){const auto& o=os[i];auto oat=at+".ores["+std::to_string(i)+"]";VoxelOre v;v.block=block(o,"block",oat,true);v.replaces=block(o,"replaces",oat,false);if(!o.has("replaces"))v.replaces=c.stone;
        v.minY=static_cast<std::int32_t>(num(o,"minY",0,-(1<<20),1<<20,oat));v.maxY=static_cast<std::int32_t>(num(o,"maxY",64,-(1<<20),1<<20,oat));
        v.veinsPerChunk=static_cast<std::uint32_t>(num(o,"veins",8,0,64,oat));v.veinSize=static_cast<std::uint32_t>(num(o,"size",8,1,64,oat));c.ores.push_back(v);}
    if(auto e=validateGen(c,r))throw JsonError(*e);
    return c;
}
}
