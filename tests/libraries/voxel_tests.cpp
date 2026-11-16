// Voxel packages tests (own CTest: library_voxel): voxel-world, voxel-light, voxel-mesh, voxel-gen, voxel-ticks,
// voxel-nav, voxel-replication.
#include <aiverselib/systems/voxel/VoxelData.h>
#include <aiverselib/systems/voxel/VoxelQuery.h>
#include <aiverselib/systems/voxel/VoxelPersistence.h>
#include <aiverselib/systems/voxellight/VoxelLight.h>
#include <aiverselib/systems/voxelmesh/VoxelMesh.h>
#include <aiverselib/systems/voxelgen/VoxelGenData.h>
#include <aiverselib/systems/voxelticks/VoxelTicksData.h>
#include <aiverselib/systems/voxelticks/VoxelTicksPersistence.h>
#include <aiverselib/bots/voxelnav/VoxelNav.h>
#include <aiverselib/net/voxel/VoxelReplication.h>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
template<class F> void expectError(F&& f,const std::string& part){
    try{f();}catch(const JsonError& e){if(std::string(e.what()).find(part)==std::string::npos)throw std::runtime_error("unexpected error text: "+std::string(e.what()));return;}
    throw std::runtime_error("expected JsonError containing "+part);
}
const char* kBlocks=R"({"blocks":[
 {"name":"stone","breakTicks":30,"drop":"cobblestone","texture":1},
 {"name":"cobblestone","texture":2},
 {"name":"dirt","texture":3,"growInto":"grass","growChance":4096},
 {"name":"grass","textures":{"side":4,"top":5,"bottom":3},"drop":"dirt"},
 {"name":"sand","texture":6,"falls":true},
 {"name":"water","fluid":1,"fluidSpread":7,"fluidTicks":5,"infiniteSource":true,"texture":7},
 {"name":"lava","fluid":2,"fluidSpread":3,"fluidTicks":30,"emission":15,"texture":8},
 {"name":"glass","layer":"translucent","opacity":0,"texture":9},
 {"name":"leaves","layer":"cutout","opacity":1,"texture":10},
 {"name":"torch","shape":"cross","solid":false,"emission":14,"texture":11},
 {"name":"tallgrass","shape":"cross","texture":12},
 {"name":"log","textures":{"side":13,"top":14}},
 {"name":"obsidian","texture":15,"breakTicks":900},
 {"name":"coal_ore","texture":16},
 {"name":"bedrock","unbreakable":true,"texture":17}
]})";
BlockRegistry blocks(){return readBlockRegistry(Json::parse(kBlocks));}
BlockId id(const BlockRegistry& r,const char* n){auto b=r.find(n);if(!b)throw std::runtime_error(std::string("unknown test block ")+n);return *b;}
VoxelChunk filled(BlockId bottom,int height){VoxelChunk c;for(int y=0;y<height;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x)c.blocks[static_cast<std::size_t>(voxelIndex(x,y,z))]=bottom;c.nonAir=countNonAir(c);return c;}
// Flat test world: chunks x,z in [-r, r], y 0..1; ground = stone up to y 7 (top surface at y = 8).
VoxelWorld flat(const BlockRegistry& r,int radius=1){
    VoxelWorld w({-1,3,1<<22,1<<16});
    for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z){w.insert({x,0,z},filled(id(r,"stone"),8));w.insert({x,1,z},VoxelChunk{});}
    for(int y=0;y<=1;++y)for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z)lightChunk(w,r,{x,y,z});
    return w;
}

void registryAndData(){
    auto r=blocks();CHECK(r.defs.size()==16);CHECK(r[0].name=="air");
    CHECK(r[id(r,"stone")].drop==id(r,"cobblestone")&&!r[id(r,"stone")].dropsSelf);
    CHECK(r[id(r,"grass")].textures[3]==5&&r[id(r,"grass")].textures[2]==3&&r[id(r,"grass")].textures[0]==4);
    CHECK(r[id(r,"water")].shape==BlockShape::Fluid&&!r[id(r,"water")].solid&&r[id(r,"water")].replaceable&&r[id(r,"water")].layer==RenderLayer::Translucent);
    CHECK(r[id(r,"torch")].layer==RenderLayer::Cutout&&!r[id(r,"torch")].solid&&r[id(r,"torch")].opacity==0);
    CHECK(!validateRegistry(r));
    expectError([]{readBlockRegistry(Json::parse(R"({"blocks":[{"name":"a","opacity":16}]})"));},"blocks[0].opacity: 0..15");
    expectError([]{readBlockRegistry(Json::parse(R"({"blocks":[{"name":"a","drop":"zzz"}]})"));},"unknown block 'zzz'");
    expectError([]{readBlockRegistry(Json::parse(R"({"blocks":[{"name":"a"},{"name":"a"}]})"));},"duplicate");
    expectError([]{readBlockRegistry(Json::parse(R"({"blocks":[{"name":"a","shape":"sphere"}]})"));},"blocks[0].shape");
}
void coordinates(){
    CHECK((chunkOf({-1,-17,16})==ChunkPos{-1,-2,1}));CHECK(voxelIndex(BlockPos{-1,-1,-1})==voxelIndex(15,15,15));
    CHECK((chunkOrigin({-2,1,0})==BlockPos{-32,16,0}));
}
void editsAndRevisions(){
    auto r=blocks();VoxelWorld w;w.insert({0,0,0},VoxelChunk{});w.renderDirty.clear();
    auto old=w.set({1,2,3},id(r,"stone"),0,r);CHECK(old&&*old==kAir);
    CHECK(w.block({1,2,3})==id(r,"stone"));CHECK(w.chunk({0,0,0})->revision==1&&w.chunk({0,0,0})->nonAir==1);
    CHECK(w.edits.size()==1&&w.edits[0].seq==1&&w.edits[0].revision==1);CHECK(w.edited.count({0,0,0})==1);
    CHECK(w.renderDirty.count({0,0,0})==1);
    CHECK(!w.set({100,0,0},id(r,"stone"),0,r));CHECK(!w.set({1,2,3},9999,0,r));
    CHECK(w.set({1,2,3},id(r,"stone"),0,r)&&w.chunk({0,0,0})->revision==1); // same block: no new revision
    // Border edit re-meshes the neighbour.
    w.insert({-1,0,0},VoxelChunk{});w.renderDirty.clear();w.set({0,5,5},id(r,"dirt"),0,r);CHECK(w.renderDirty.count({-1,0,0})==1);
    // Overflow of pending edits.
    VoxelWorld small({-4,19,1<<22,4});small.insert({0,0,0},VoxelChunk{});for(int i=0;i<6;++i)small.set({i,0,0},id(r,"dirt"),0,r);
    CHECK(small.editOverflow&&small.edits.size()<=4);
}
void codecAndRegions(){
    auto r=blocks();auto c=filled(id(r,"stone"),5);c.blocks[100]=id(r,"lava");c.meta[100]=3;c.revision=42;
    auto bytes=encodeChunk(c);CHECK(bytes.size()<64);
    auto back=decodeChunk(bytes,r.defs.size());CHECK(back&&back->blocks==c.blocks&&back->meta==c.meta&&back->revision==42&&back->nonAir==c.nonAir);
    CHECK(encodeChunk(*back)==bytes);
    CHECK(!decodeChunk(bytes,3)); // lava id unknown with 3 types
    for(std::size_t cut=0;cut<bytes.size();++cut){std::vector<std::uint8_t> part(bytes.begin(),bytes.begin()+static_cast<long>(cut));CHECK(!decodeChunk(part,r.defs.size()));}
    auto extra=bytes;extra.push_back(0);CHECK(!decodeChunk(extra,r.defs.size()));
    std::map<ChunkPos,std::vector<std::uint8_t>> region{{{0,0,0},bytes},{{1,-2,3},encodeChunk(VoxelChunk{})}};
    auto file=encodeRegion(region);auto again=decodeRegion(file,r.defs.size());CHECK(again&&*again==region);
    auto broken=file;broken[5]^=1;CHECK(!decodeRegion(broken,r.defs.size()));
    CHECK((regionOf({-1,0,33})==RegionPos{-1,1}));
}
void parkingAndSave(){
    auto r=blocks();auto w=flat(r,1);auto before=w.hash();
    w.set({3,8,3},id(r,"glass"),0,r);w.set({20,8,3},id(r,"sand"),0,r);
    CHECK(w.edited.size()==2);auto edited=w.hash();CHECK(edited!=before);
    // Unload parks the edited chunk; unpark brings it back.
    CHECK(w.unload({0,0,0}));CHECK(w.parked({0,0,0})&&!w.loaded({0,0,0}));
    auto again=w.unpark({0,0,0},r);CHECK(again&&again->blocks[static_cast<std::size_t>(voxelIndex(3,8,3))]==id(r,"glass"));
    w.insert({0,0,0},*again);lightChunk(w,r,{0,0,0});CHECK(w.hash()==edited);
    // Regions round trip into another world.
    auto regions=w.exportRegions();CHECK(regions.size()==1);
    VoxelWorld other({-1,3,1<<22,1<<16});CHECK(other.importRegion(regions.begin()->second,r));CHECK(other.parked({0,0,0})&&other.parked({1,0,0}));
    // NativeState save: resident list + edited chunks; reload the same resident set and compare hashes.
    game::NativeState state;CHECK(storeVoxelWorld(state,"voxel",w));
    auto saved=loadVoxelWorld(state,"voxel",r);CHECK(saved&&saved->resident.size()==w.chunks().size());
    auto& restored=saved->world;
    for(auto c:saved->resident){if(restored.parked(c))restored.insert(c,*restored.unpark(c,r));else restored.insert(c,c.y==0?filled(id(r,"stone"),8):VoxelChunk{});}
    CHECK(restored.hash()==w.hash());
    auto bad=state;bad.arrays["voxel"].back()^=1;CHECK(!loadVoxelWorld(bad,"voxel",r));
    auto cut=state;cut.arrays["voxel"].pop_back();CHECK(!loadVoxelWorld(cut,"voxel",r));
}
void queries(){
    auto r=blocks();auto w=flat(r,1);
    // Ray straight down from above hits the stone top at y=7 through its +Y face, placement cell y=8.
    auto hit=voxelRaycastSolid(w,r,{Fixed::fromRatio(1,2),20_fx,Fixed::fromRatio(1,2)},{Fixed{},-30_fx,Fixed{}});
    CHECK(hit.hit&&(hit.block==BlockPos{0,7,0})&&(hit.before==BlockPos{0,8,0})&&hit.face==3);
    auto miss=voxelRaycastSolid(w,r,{0_fx,20_fx,0_fx},{5_fx,0_fx,0_fx});CHECK(!miss.hit&&!miss.unloaded);
    auto far=voxelRaycastSolid(w,r,{0_fx,20_fx,0_fx},{200_fx,0_fx,0_fx});CHECK(far.unloaded&&!far.hit);
    auto diag=voxelRaycastSolid(w,r,{Fixed::fromRatio(1,2),9_fx,Fixed::fromRatio(1,2)},{4_fx,-4_fx,3_fx});CHECK(diag.hit&&diag.block.y==7);
    // A 0.6 x 1.8 box falls onto the ground and stops exactly on y = 8.
    VoxelBox box{{Fixed::fromRatio(2,10),10_fx,Fixed::fromRatio(2,10)},{Fixed::fromRatio(8,10),Fixed::fromRatio(118,10),Fixed::fromRatio(8,10)}};
    auto fall=voxelMoveBox(w,r,box,{Fixed{},-5_fx,Fixed{}});CHECK(fall.grounded&&fall.delta.y==-2_fx);
    box.min.y=8_fx;box.max.y=Fixed::fromRatio(98,10);CHECK(voxelGrounded(w,r,box));CHECK(!voxelOverlapsSolid(w,r,box));
    // Walking into a wall stops at its face; the other axis keeps moving.
    w.set({2,8,0},id(r,"stone"),0,r);
    auto walk=voxelMoveBox(w,r,box,{3_fx,Fixed{},Fixed::fromRatio(1,10)});CHECK(walk.hit[0]&&walk.delta.x==Fixed::fromRatio(12,10)&&walk.delta.z==Fixed::fromRatio(1,10));
    CHECK(voxelMoveBox(w,r,box,{20_fx,Fixed{},Fixed{}}).tooFar);
    // A box inside a block can leave it.
    VoxelBox stuck{{Fixed::fromRatio(21,10),Fixed::fromRatio(81,10),Fixed::fromRatio(1,10)},{Fixed::fromRatio(29,10),9_fx,Fixed::fromRatio(9,10)}};
    CHECK(voxelMoveBox(w,r,stuck,{1_fx,Fixed{},Fixed{}}).delta.x==1_fx);
}
void light(){
    auto r=blocks();auto w=flat(r,1);
    CHECK(skyLight(w.light({0,8,0}))==15&&skyLight(w.light({0,7,0}))==0&&skyLight(w.light({0,30,0}))==15);
    // A roof shades the cell under it; light comes in sideways and loses 1 per block.
    for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z){BlockPos p{x,12,z};auto old=*w.set(p,id(r,"stone"),0,r);relightBlock(w,r,p,old);}
    CHECK(skyLight(w.light({0,11,0}))==15-4);CHECK(skyLight(w.light({0,8,0}))==15-4);
    // A torch under the roof.
    {BlockPos t{0,8,0};auto old=*w.set(t,id(r,"torch"),0,r);relightBlock(w,r,t,old);}
    CHECK(blockLight(w.light({0,8,0}))==14&&blockLight(w.light({3,8,0}))==11&&blockLight(w.light({0,8,-2}))==12);
    CHECK(voxelLightLevel(w,{0,8,0})==14);
    // Removing the torch removes its light, and only its light.
    {BlockPos t{0,8,0};auto old=*w.set(t,kAir,0,r);relightBlock(w,r,t,old);}
    CHECK(blockLight(w.light({0,8,0}))==0&&blockLight(w.light({3,8,0}))==0);CHECK(skyLight(w.light({0,8,0}))==11);
    // Opening a hole in the roof brings full sky down to the ground.
    {BlockPos h{0,12,0};auto old=*w.set(h,kAir,0,r);relightBlock(w,r,h,old);}
    CHECK(skyLight(w.light({0,8,0}))==15&&skyLight(w.light({1,8,0}))==14);
    // Glass passes sky light straight down; leaves reduce it.
    {BlockPos h{0,12,0};auto old=*w.set(h,id(r,"glass"),0,r);relightBlock(w,r,h,old);}CHECK(skyLight(w.light({0,8,0}))==15);
    {BlockPos h{0,12,0};auto old=*w.set(h,id(r,"leaves"),0,r);relightBlock(w,r,h,old);}CHECK(skyLight(w.light({0,11,0}))<15);
    // Incremental light equals a full recompute.
    auto copy=w;for(const auto& [c,chunk]:w.chunks())(void)chunk,copy.lightDirty.insert(c);
    for(const auto& [c,chunk]:w.chunks())(void)chunk,copy.chunk(c)->light.fill(0);
    while(lightPending(copy,r,1000)){}
    for(const auto& [c,chunk]:w.chunks())CHECK(copy.chunk(c)->light==chunk.light);
    // A chunk inserted above shades the column below that assumed open sky.
    VoxelWorld tower({-1,3,1<<22,1<<16});tower.insert({0,0,0},VoxelChunk{});lightChunk(tower,r,{0,0,0});CHECK(skyLight(tower.light({4,0,4}))==15);
    tower.insert({0,1,0},filled(id(r,"stone"),16));lightChunk(tower,r,{0,1,0});CHECK(skyLight(tower.light({4,0,4}))==0);
}

void checkWinding(const VoxelMeshLayer& L){
    for(std::size_t t=0;t<L.indices.size();t+=3){const auto& a=L.vertices[L.indices[t]];const auto& b=L.vertices[L.indices[t+1]];const auto& c=L.vertices[L.indices[t+2]];
        float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;CHECK(nx*a.nx+ny*a.ny+nz*a.nz>0);}
}
void mesh(){
    auto r=blocks();VoxelMeshOptions o;o.unloadedOpaque=false;
    VoxelWorld w;w.insert({0,0,0},VoxelChunk{});
    w.set({4,4,4},id(r,"grass"),0,r);for(auto c:std::vector<ChunkPos>{{0,0,0}})lightChunk(w,r,c);
    auto one=meshChunk(w,r,{0,0,0},o);CHECK(one.layers[kVoxelOpaque].quads()==6&&one.layers[kVoxelOpaque].vertices.size()==24);checkWinding(one.layers[kVoxelOpaque]);
    for(const auto& v:one.layers[kVoxelOpaque].vertices){if(v.ny==1)CHECK(v.texture==5);if(v.ny==-1)CHECK(v.texture==3);if(v.nx||v.nz)CHECK(v.texture==4);}
    // Side textures upright: on every side face, the upper vertices have the larger v.
    for(const auto& v:one.layers[kVoxelOpaque].vertices)if(v.nx||v.nz)CHECK((v.y>4.5f)==(v.v>0.5f));
    // Two neighbours merge into a 2x1x1 box: still 6 quads, 2 blocks long.
    {auto old=*w.set({5,4,4},id(r,"grass"),0,r);relightBlock(w,r,{5,4,4},old);}
    VoxelMeshOptions flatLight=o;flatLight.smoothLight=false;
    auto two=meshChunk(w,r,{0,0,0},flatLight);CHECK(two.layers[kVoxelOpaque].quads()==6);
    float maxU=0;for(const auto& v:two.layers[kVoxelOpaque].vertices)if(v.ny==1)maxU=std::max({maxU,v.u,v.v});CHECK(maxU==2.f);
    VoxelMeshOptions noGreedy=flatLight;noGreedy.greedy=false;CHECK(meshChunk(w,r,{0,0,0},noGreedy).layers[kVoxelOpaque].quads()==10);
    // A full flat layer merges into one quad per side.
    VoxelWorld f;f.insert({0,0,0},filled(id(r,"stone"),1));lightChunk(f,r,{0,0,0});
    auto floor=meshChunk(f,r,{0,0,0},flatLight); // smooth light would darken edges next to unloaded air
   CHECK(floor.layers[kVoxelOpaque].quads()==6);checkWinding(floor.layers[kVoxelOpaque]);
    // AO: a block standing on the floor darkens the floor corners next to it, so the top no longer merges into one.
    f.set({8,1,8},id(r,"stone"),0,r);lightChunk(f,r,{0,0,0});auto ao=meshChunk(f,r,{0,0,0},o);
    bool dark=false;for(const auto& v:ao.layers[kVoxelOpaque].vertices)if(v.ny==1&&v.y<1.5f&&v.ao<3)dark=true;CHECK(dark);
    VoxelMeshOptions flatAo=o;flatAo.ambientOcclusion=false;flatAo.smoothLight=false;
    CHECK(meshChunk(f,r,{0,0,0},flatAo).layers[kVoxelOpaque].quads()<ao.layers[kVoxelOpaque].quads());
    // Layers: glass and water translucent, leaves cutout, torch = 4 cross quads; glass next to glass hides the shared face.
    VoxelWorld m;m.insert({0,0,0},VoxelChunk{});m.set({1,1,1},id(r,"glass"),0,r);m.set({2,1,1},id(r,"glass"),0,r);m.set({5,5,5},id(r,"leaves"),0,r);
    m.set({8,8,8},id(r,"torch"),0,r);m.set({10,2,10},id(r,"water"),3,r);lightChunk(m,r,{0,0,0});
    auto layers=meshChunk(m,r,{0,0,0},o);
    CHECK(layers.layers[kVoxelCutout].quads()==6+4);CHECK(layers.layers[kVoxelTranslucent].quads()==6+6);checkWinding(layers.layers[kVoxelCutout]);
    float waterTop=0;for(const auto& v:layers.layers[kVoxelTranslucent].vertices)if(v.texture==7)waterTop=std::max(waterTop,v.y);CHECK(waterTop>2.4f&&waterTop<3.f);
    // Mesh input copies the neighbourhood: meshing the copy equals meshing the world.
    auto in=voxelMeshInput(m,{0,0,0});auto copy=meshChunk(in,r,o);CHECK(copy.layers[2].indices==layers.layers[2].indices);
    sortTranslucent(copy.layers[2],0,0,0);CHECK(copy.layers[2].indices.size()==layers.layers[2].indices.size());
    // Unloaded neighbours count as opaque by default: a stone floor at a chunk edge emits no outward side faces.
    auto closed=meshChunk(f,r,{0,0,0});CHECK(closed.layers[kVoxelOpaque].quads()<ao.layers[kVoxelOpaque].quads());
}

const char* kGen=R"({"voxelGen":{"seed":12345,"seaLevel":62,"baseHeight":66,"heightAmplitude":20,"heightScale":96,"octaves":4,"bedrockY":-64,
 "stone":"stone","water":"water","bedrock":"bedrock","caves":{"scale":20,"minY":-60,"maxY":100,"threshold":42000},"biomeScale":48,
 "biomes":[{"name":"plains","upTo":700,"surface":"grass","filler":"dirt","shore":"sand","trees":3,"log":"log","leaves":"leaves","plants":24,"plant":"tallgrass"},
           {"name":"desert","upTo":1000,"surface":"sand","filler":"sand","heightOffset":-2}],
 "ores":[{"block":"coal_ore","minY":-60,"maxY":100,"veins":12,"size":10}]}})";
void generation(){
    auto r=blocks();VoxelGenerator g(readVoxelGen(Json::parse(kGen),r));
    // Same chunk, same bytes; any order of generation gives the same chunk.
    auto a=g.generate({1,3,-2});auto b=g.generate({1,3,-2});CHECK(encodeChunk(a)==encodeChunk(b));
    VoxelGenerator again(readVoxelGen(Json::parse(kGen),r));for(int i=0;i<3;++i)(void)again.generate({i,4,i});CHECK(encodeChunk(again.generate({1,3,-2}))==encodeChunk(a));
    // Golden hash: catches unintended changes of the generator (update only on purpose, with a note in docs).
    VoxelHasher h;for(int cy=2;cy<=5;++cy)for(int cx=-1;cx<=1;++cx)for(int cz=-1;cz<=1;++cz){auto c=g.generate({cx,cy,cz});for(auto v:c.blocks)h.u32(v);}
    CHECK(h.h==1183114399492830062ull); // golden: change only on purpose and say so in docs/voxel.md
    // Surface: the column top is solid at heightAt and air or water above; bedrock at the bottom.
    std::map<std::string,int> seen;for(int x=-40;x<40;++x)for(int z=-40;z<40;++z)seen[g.biomeAt(x,z).name]++;CHECK(seen.size()==2);
    int land=0,trees=0,plants=0,ores=0,caves=0;
    for(int cx=-2;cx<=2;++cx)for(int cz=-2;cz<=2;++cz)for(int cy=-4;cy<=6;++cy){auto c=g.generate({cx,cy,cz});auto o=chunkOrigin({cx,cy,cz});
        for(int i=0;i<kVoxelChunkVolume;++i){auto id_=c.blocks[static_cast<std::size_t>(i)];int x=o.x+(i&15),y=o.y+(i>>8),z=o.z+((i>>4)&15);
            if(id_==id(r,"log"))++trees;if(id_==id(r,"tallgrass"))++plants;if(id_==id(r,"coal_ore"))++ores;
            if(y==-64)CHECK(id_==id(r,"bedrock"));
            auto hh=g.heightAt(x,z);if(y==hh&&hh>g.config().seaLevel+1&&id_!=kAir)++land;
            if(y<hh-3&&y>-60&&id_==kAir)++caves;
            if(y>hh+24)CHECK(id_==kAir);}} // leaves of a tree on a higher neighbour column may overhang
CHECK(land>100&&trees>10&&plants>10&&ores>50&&caves>50);
    // Trees across chunk borders: a log's chunk neighbour holds the leaves of the same tree.
    expectError([&]{readVoxelGen(Json::parse(R"({"voxelGen":{"stone":"stone","biomes":[{"surface":"nope","filler":"dirt"}]}})"),r);},"voxelGen.biomes[0].surface: unknown block 'nope'");
    expectError([&]{readVoxelGen(Json::parse(R"({"voxelGen":{"stone":"stone","biomes":[{"upTo":500,"surface":"grass","filler":"dirt"}]}})"),r);},"last upTo must be 1000");
    // Noise stays in range and is continuous at cell borders.
    for(int x=-300;x<300;x+=7){auto n=voxelNoise2(1,x,x*3,32,0);CHECK(n>=0&&n<65536);auto d=voxelNoise2(1,x,0,32,0)-voxelNoise2(1,x+1,0,32,0);CHECK(d<4096&&d>-4096);}
}

struct TickRig{
    BlockRegistry r=blocks();VoxelWorld w=flat(r,1);VoxelTickRules rules=readVoxelTicks(Json::parse(R"({"voxelTicks":{"randomTicksPerChunk":0,"reactions":[{"fluid":"lava","touching":"water","source":"obsidian","flowing":"cobblestone"}]}})"),r);
    VoxelTickState s;Random rng{5};std::uint64_t tick=0;
    void put(BlockPos p,const char* n,std::uint8_t meta=0){auto old=w.set(p,id(r,n),meta,r);CHECK(old);relightBlock(w,r,p,*old);notifyVoxelChange(s,w,r,rules,p,tick);}
    void run(int ticks){for(int i=0;i<ticks;++i){auto rep=stepVoxelTicks(rules,s,w,r,++tick,rng);CHECK(rep);for(auto c:rep->changed)relightBlock(w,r,c.pos,c.oldBlock);}}
};
void ticks(){
    TickRig t;auto water=id(t.r,"water");
    t.put({0,8,0},"water");t.run(200);
    CHECK(t.w.block({7,8,0})==water&&t.w.meta({7,8,0})==7&&t.w.block({8,8,0})==kAir);CHECK(t.w.meta({3,8,0})==3&&t.w.meta({2,8,2})==4);
    CHECK(t.s.queue.empty());
    // Removing the source drains the flow.
    t.put({0,8,0},"stone");t.put({0,8,0},"air");t.run(200);CHECK(t.w.block({3,8,0})==kAir&&t.w.block({7,8,0})==kAir);
    // A hole: water falls into it (falling meta 8) and spreads at the bottom.
    t.put({4,7,4},"air");t.put({4,6,4},"air");t.put({3,8,4},"water");t.run(200);
    CHECK(t.w.block({4,6,4})==water&&(t.w.meta({4,7,4})&8));
    // Infinite source: two sources fill the gap between them on solid ground.
    TickRig u;u.put({0,8,0},"water");u.put({2,8,0},"water");u.run(100);CHECK(u.w.block({1,8,0})==water&&u.w.meta({1,8,0})==0);
    // Sand falls to the ground.
    TickRig f;f.put({5,12,5},"sand");f.run(30);CHECK(f.w.block({5,8,5})==id(f.r,"sand")&&f.w.block({5,12,5})==kAir);
    // Lava next to water: the lava source becomes obsidian, flowing lava cobblestone.
    TickRig l;l.put({0,8,0},"lava");l.run(200);CHECK(l.w.block({2,8,0})==id(l.r,"lava"));
    l.put({0,9,0},"water");l.run(300); // water poured onto the lava source
    bool obsidian=l.w.block({0,8,0})==id(l.r,"obsidian"),cobble=false;for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)cobble|=l.w.block({x,8,z})==id(l.r,"cobblestone");
    CHECK(obsidian&&cobble);
    // Same input, same world (determinism), and the save record round trips.
    TickRig a,b;for(auto* rig:{&a,&b}){rig->put({0,8,0},"water");rig->put({6,8,6},"lava");rig->run(37);}
    CHECK(a.w.hash()==b.w.hash()&&voxelTicksHash(a.s)==voxelTicksHash(b.s));
    game::NativeState st;CHECK(storeVoxelTicks(st,"ticks",a.s));auto back=loadVoxelTicks(st,"ticks");CHECK(back&&voxelTicksHash(*back)==voxelTicksHash(a.s));
    a.run(80);b.s=*back;b.run(80);CHECK(a.w.hash()==b.w.hash());
    auto bad=st;bad.arrays["ticks"][5]+=1;CHECK(!loadVoxelTicks(bad,"ticks"));
    CHECK(!stepVoxelTicks(a.rules,a.s,a.w,a.r,a.tick,a.rng)); // tick must increase
    // Budget: at most maxUpdatesPerStep updates per step, the rest stays queued.
    TickRig g;g.rules.maxUpdatesPerStep=3;g.put({0,8,0},"water");for(int i=0;i<8;++i)g.run(1);CHECK(!g.s.queue.empty());
    // Random ticks: dirt under open sky grows into grass (growChance 4096 = every hit).
    TickRig rt;rt.rules.randomTicksPerChunk=4096;for(int x=0;x<16;++x)for(int z=0;z<16;++z)rt.put({x,7,z},"dirt");
    rt.run(3);int grass=0;for(int x=0;x<16;++x)for(int z=0;z<16;++z)grass+=rt.w.block({x,7,z})==id(rt.r,"grass");CHECK(grass>100);
    expectError([&]{readVoxelTicks(Json::parse(R"({"voxelTicks":{"reactions":[{"fluid":"stone","touching":"water"}]}})"),rt.r);},"unknown fluid block 'stone'");
}

void navigation(){
    auto r=blocks();auto w=flat(r,1);bots::VoxelAgent mob;
    auto straight=bots::voxelPath(w,r,mob,{0,8,0},{10,8,0});CHECK(straight&&straight->path.size()==11&&(straight->path.back()==BlockPos{10,8,0}));
    // A wall 3 high blocks the way: the mob walks around it; a 1-high step is climbed.
    for(int z=-6;z<=6;++z)for(int y=8;y<=10;++y)w.set({5,y,z},id(r,"stone"),0,r);
    auto around=bots::voxelPath(w,r,mob,{0,8,0},{10,8,0});CHECK(around&&around->path.size()>11);
    for(const auto& p:around->path)CHECK(p.x!=5||p.z< -6||p.z>6);
    w.set({2,8,10},id(r,"stone"),0,r);auto step=bots::voxelPath(w,r,mob,{0,8,10},{4,8,10});CHECK(step);bool climbed=false;for(auto p:step->path)climbed|=p.y==9;CHECK(climbed);
    // Too high to climb, no way around inside the budget: no path, and the budget is reported.
    bots::VoxelAgent tiny=mob;tiny.maxNodes=20;CHECK(!bots::voxelPath(w,r,tiny,{0,8,0},{10,8,0}));
    // Dropping down a 3-block ledge is allowed, 4 is not.
    VoxelWorld cliff=flat(r,1);for(int x=-16;x<0;++x)for(int z=-16;z<32;++z)for(int y=8;y<=10;++y)cliff.set({x,y,z},id(r,"stone"),0,r);
    CHECK(bots::voxelPath(cliff,r,mob,{-3,11,0},{3,8,0}));
    bots::VoxelAgent careful=mob;careful.maxDrop=2;CHECK(!bots::voxelPath(cliff,r,careful,{-3,11,0},{3,8,0}));
    // Diagonals shorten the path; deterministic: same query, same path.
    bots::VoxelAgent diag=mob;diag.diagonal=true;auto d1=bots::voxelPath(w,r,diag,{-8,8,-8},{-2,8,-2});auto d2=bots::voxelPath(w,r,diag,{-8,8,-8},{-2,8,-2});
    CHECK(d1&&d2&&d1->path==d2->path&&d1->path.size()==7);
    // Spawn spots: dark cells only. Under a stone roof it is dark; in the open sky it is not.
    for(int x=-14;x<=14;++x)for(int z=-14;z<=14;++z){BlockPos p{x,11,z};auto old=w.set(p,id(r,"stone"),0,r);if(old)relightBlock(w,r,p,*old);}
    Random rng(3);bots::VoxelSpawnRule rule;rule.radius=12;rule.minDistance=2;rule.verticalRange=4;rule.attempts=400;
    auto spots=bots::voxelSpawnSpots(w,r,mob,rule,{0,8,0},8,rng);CHECK(!spots.empty());
    for(auto p:spots)CHECK(voxelLightLevel(w,p)<=7&&p.y==8);
    VoxelWorld open=flat(r,1);Random rng2(3);CHECK(bots::voxelSpawnSpots(open,r,mob,rule,{0,8,0},8,rng2).empty());
    rule.skyDarkening=11;Random rng3(3);CHECK(!bots::voxelSpawnSpots(open,r,mob,rule,{0,8,0},8,rng3).empty()); // night
}

void replication(){
    auto r=blocks();VoxelGenerator g(readVoxelGen(Json::parse(kGen),r));
    VoxelWorld server({-4,7,1<<22,1<<16});for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)for(int y=-4;y<=7;++y)server.insert({x,y,z},g.generate({x,y,z}));
    VoxelWorld client({-4,7,1<<22,1<<16});net::VoxelPeer peer;net::VoxelView view{{0,4,0},2,2,2};net::VoxelPumpConfig cfg;cfg.maxChunks=10;
    auto deliver=[&](const std::vector<std::vector<std::uint8_t>>& msgs){for(const auto& m:msgs){std::vector<BlockId> old;auto res=net::applyVoxelMessage(client,r,m,&old);CHECK(res.ok);
        for(std::size_t i=0;i<res.applied.size();++i)relightBlock(client,r,res.applied[i].pos,old[i]);}lightPending(client,r,1000);};
    net::VoxelPumpStats st;
    // Budget: 10 chunks per pump, nearest first; repeated pumps fill the whole view (13 columns x 5 levels).
    deliver(net::pumpVoxelPeer(cfg,server,view,peer,{},&st));CHECK(st.chunks==10&&st.pendingChunks==55);CHECK(client.loaded({0,4,0}));
    for(int i=0;i<10;++i)deliver(net::pumpVoxelPeer(cfg,server,view,peer,{},&st));CHECK(client.chunks().size()==65);
    for(const auto& [c,chunk]:client.chunks())CHECK(chunk.blocks==server.chunk(c)->blocks);
    // Edits travel as small messages and keep the mirror equal.
    server.edits.clear();server.set({1,70,1},id(r,"glass"),0,r);server.set({1,71,1},id(r,"torch"),0,r);server.set({17,70,0},id(r,"stone"),0,r);
    auto msgs=net::pumpVoxelPeer(cfg,server,view,peer,server.edits,&st);CHECK(st.edits==3&&st.chunks==0&&msgs.size()==1&&msgs[0].size()<64);deliver(msgs);
    CHECK(client.block({1,71,1})==id(r,"torch")&&blockLight(client.light({1,71,2}))==13);
    // A lost edit (gap) makes the server resend that chunk whole.
    server.edits.clear();server.set({2,70,2},id(r,"stone"),0,r);server.set({3,70,2},id(r,"stone"),0,r);
    std::vector<BlockEdit> lost{server.edits[1]};msgs=net::pumpVoxelPeer(cfg,server,view,peer,lost,&st);CHECK(st.resends==1&&st.chunks==1);deliver(msgs);
    CHECK(client.block({2,70,2})==id(r,"stone")&&client.chunk({0,4,0})->revision==server.chunk({0,4,0})->revision);
    // Moving the view unloads chunks behind and streams new ones in.
    view.center={3,4,0};for(int i=0;i<12;++i)deliver(net::pumpVoxelPeer(cfg,server,view,peer,{},&st));
    CHECK(!client.loaded({-2,4,0})&&client.loaded({5,4,0})==server.loaded({5,4,0}));
    for(const auto& [c,chunk]:client.chunks())CHECK(net::voxelInView(view,c)&&chunk.blocks==server.chunk(c)->blocks);
    // Malformed bytes are rejected.
    auto bytes=net::encodeVoxelMessage({net::VoxelMessageType::Unload,{1,2,3},{},{}});CHECK(net::decodeVoxelMessage(bytes,r.defs.size()));
    bytes.push_back(1);CHECK(!net::decodeVoxelMessage(bytes,r.defs.size()));CHECK(!net::applyVoxelMessage(client,r,{'V','M',9}).ok);
}

// Acceptance W2 (docs/genre-implementation.json): digging a tunnel and building a floor update light, paths,
// water and the save.
void acceptanceW2(){
    TickRig t;auto& w=t.w;auto& r=t.r;bots::VoxelAgent mob;
    // A stone wall 6 high across the map splits it in two.
    for(int z=-16;z<32;++z)for(int y=8;y<=13;++y){BlockPos p{4,y,z};auto old=*w.set(p,id(r,"stone"),0,r);relightBlock(w,r,p,old);}
    CHECK(!bots::voxelPath(w,r,mob,{0,8,0},{8,8,0}));
    // Dig a 1x2 tunnel through it: the inside is darker than the open sky, the path appears.
    for(int y=8;y<=9;++y)t.put({4,y,0},"air");
    CHECK(skyLight(w.light({4,8,0}))<15&&skyLight(w.light({4,8,0}))>0);
    auto path=bots::voxelPath(w,r,mob,{0,8,0},{8,8,0});CHECK(path);bool through=false;for(auto p:path->path)through|=p.x==4;CHECK(through);
    // Build a floor (roof) over the far side: the cells under it lose full sky.
    for(int x=5;x<=12;++x)for(int z=-4;z<=4;++z)t.put({x,12,z},"cobblestone");
    CHECK(skyLight(w.light({8,8,0}))<15);
    // Water poured on the near side flows through the tunnel.
    t.put({1,8,0},"water");t.run(300);CHECK(w.block({4,8,0})==id(r,"water"));CHECK(w.block({6,8,0})==id(r,"water"));
    // Save and restore world + pending updates: same hash, and the continuation is identical.
    game::NativeState st;CHECK(storeVoxelWorld(st,"world",w)&&storeVoxelTicks(st,"ticks",t.s));
    auto saved=loadVoxelWorld(st,"world",r);auto ticks=loadVoxelTicks(st,"ticks");CHECK(saved&&ticks);
    for(auto c:saved->resident)saved->world.insert(c,saved->world.parked(c)?*saved->world.unpark(c,r):(c.y==0?filled(id(r,"stone"),8):VoxelChunk{}));
    lightPending(saved->world,r,1000);CHECK(saved->world.hash()==w.hash());
    for(const auto& [c,chunk]:w.chunks())CHECK(saved->world.chunk(c)->light==chunk.light);
    TickRig u;u.w=saved->world;u.s=*ticks;u.tick=t.tick;u.rng=t.rng;
    t.put({0,13,0},"sand");u.put({0,13,0},"sand");t.run(50);u.run(50);CHECK(t.w.hash()==u.w.hash()&&voxelTicksHash(t.s)==voxelTicksHash(u.s));
}
}
int main(){
    try{registryAndData();coordinates();editsAndRevisions();codecAndRegions();parkingAndSave();queries();light();mesh();generation();ticks();navigation();replication();acceptanceW2();}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    std::cout<<"voxel tests passed\n";return 0;
}
