#include "engine/world/TerraWeaveArborMesh.h"
#include "engine/world/TerraWeaveTreePresets.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

using namespace aiverse::terraweave;

namespace {
void sameTree(const TreeSkeleton& a, const TreeSkeleton& b) {
    assert(a.speciesId == b.speciesId);
    assert(a.seed == b.seed);
    assert(a.heightCm == b.heightCm);
    assert(a.crownRadiusCm == b.crownRadiusCm);
    assert(a.branches == b.branches);
    assert(a.leaves == b.leaves);
}
void validateGraph(const TreeSkeleton& tree, bool weeping=false) {
    assert(!tree.branches.empty());
    assert(tree.branches.front().parent == -1);
    assert(tree.branches.front().y0 == 0);
    assert(tree.branches.front().y1 == tree.heightCm);
    for (size_t i = 1; i < tree.branches.size(); ++i) {
        const TreeBranch& b = tree.branches[i];
        assert(b.parent >= 0 && static_cast<size_t>(b.parent) < i);
        assert(b.startRadiusCm > 0 && b.endRadiusCm > 0);
        assert(b.y0 >= 0 && b.y0 <= tree.heightCm);
        assert(b.y1 >= 0);
        if(!weeping)assert(b.y1 >= b.y0);
        assert(std::abs(static_cast<int64_t>(b.x1)) <= tree.crownRadiusCm);
        assert(std::abs(static_cast<int64_t>(b.z1)) <= tree.crownRadiusCm);
    }
}
void testTreeShapesAndLods() {
    for (CrownShape shape : {CrownShape::Broadleaf, CrownShape::Conifer,
                             CrownShape::Columnar}) {
        TreeProfile profile;
        profile.speciesId = 42;
        profile.shape = shape;
        profile.primaryBranches = 25;
        const uint64_t seed = 0xab83f13922334455ULL;
        auto high = generateTree(profile, seed, TreeLOD::High);
        auto med = generateTree(profile, seed, TreeLOD::Medium);
        auto low = generateTree(profile, seed, TreeLOD::Low);
        auto impostor = generateTree(profile, seed, TreeLOD::Impostor);
        validateGraph(high);
        validateGraph(med);
        validateGraph(low);
        assert(high.heightCm == med.heightCm && med.heightCm == low.heightCm);
        assert(low.heightCm == impostor.heightCm);
        assert(high.crownRadiusCm == impostor.crownRadiusCm);
        assert(high.branches.size() == 26);
        assert(med.branches.size() == 14);
        assert(low.branches.size() == 8);
        assert(high.leaves.size() == 50);
        assert(med.leaves.size() == 13);
        assert(low.leaves.size() == 7);
        assert(impostor.branches.empty() && impostor.leaves.empty());
        sameTree(high, generateTree(profile, seed, TreeLOD::High));
        auto changed = generateTree(profile, seed + 1, TreeLOD::High);
        assert(high.branches != changed.branches || high.heightCm != changed.heightCm);
    }
}
void testInstancesAndIndependentWorkers() {
    TreeProfile profile;
    profile.speciesId = 11;
    FoliageInstance instance;
    instance.speciesId = profile.speciesId;
    instance.key = 0x12345678abcdefULL;
    auto original = generateTree(profile, instance, TreeLOD::High);
    std::vector<std::future<TreeSkeleton>> jobs;
    for (int i = 0; i < 8; ++i)
        jobs.push_back(std::async(std::launch::async, [=] {
            return generateTree(profile, instance, TreeLOD::High);
        }));
    for (auto& job : jobs) sameTree(original, job.get());
    // World position is intentionally NOT baked into local-space tree skeleton.
    instance.xCm = -123456;
    instance.zCm = 99999;
    instance.yawMilliDegrees = 180000;
    sameTree(original, generateTree(profile, instance, TreeLOD::High));
}
void testGeneratedGeometry() {
    TreeProfile profile;
    profile.primaryBranches = 12;
    for (TreeLOD tier : {TreeLOD::High, TreeLOD::Medium, TreeLOD::Low, TreeLOD::Impostor}) {
        const TreeSkeleton tree = generateTree(profile, 777, tier);
        const ArborMesh mesh = buildArborMesh(tree);
        if (tier == TreeLOD::Impostor) {
            assert(mesh.vertices.empty() && mesh.indices.empty());
            continue;
        }
        assert(!mesh.vertices.empty() && !mesh.indices.empty());
        assert(mesh.indices.size() % 3 == 0);
        bool bark = false, leaf = false;
        for (const auto& v : mesh.vertices) {
            bark |= v.material == 0;
            leaf |= v.material == 1;
            assert(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
            assert(std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz));
            assert(v.material <= 1);
        }
        assert(bark && leaf);
        for (uint32_t idx : mesh.indices) assert(idx < mesh.vertices.size());
    }
    bool threw = false;
    try { (void)buildArborMesh(generateTree(profile, 42), {2, true}); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
void testInvalidProfiles() {
    TreeProfile profile;
    bool threw = false;
    profile.minHeightCm = 0;
    try { (void)generateTree(profile, 3); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    profile.minHeightCm = 800;
    profile.primaryBranches = 300;
    threw = false;
    try { (void)generateTree(profile, 3); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    profile.primaryBranches = 20;
    FoliageInstance wrongSpecies;
    wrongSpecies.speciesId = 999;
    threw = false;
    try { (void)generateTree(profile, wrongSpecies, TreeLOD::Medium); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
}
int main() {
    {
        const auto& preset=botanicalTreePresets()[4];
        ArborMeshSettings mesh;mesh.naturalBroadleaf=true;mesh.organicRootFlare=true;
        const auto tree=generateTree(preset.profile,2718);
        const auto geometry=buildArborMesh(tree,mesh);
        float shortest=1e9f,longest=0;
        size_t feet=0;
        for(const auto& v:geometry.vertices) {
            assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
            assert(std::isfinite(v.u)&&std::isfinite(v.v));
            const float normal=v.nx*v.nx+v.ny*v.ny+v.nz*v.nz;
            assert(std::abs(normal-1.f)<.002f);
            if(v.material==0&&v.y< -14.9f) {
                const float radius=std::hypot(v.x,v.z);
                shortest=std::min(shortest,radius);longest=std::max(longest,radius);++feet;
            }
        }
        // A buried continuous ring, with seeded lobes rather than capped spokes.
        assert(feet>=25&&longest>shortest*1.3f);
        assert(std::all_of(geometry.indices.begin(),geometry.indices.end(),
            [&](uint32_t i){return i<geometry.vertices.size();}));
        for(size_t i=0;i<geometry.indices.size();i+=3) {
            const auto& a=geometry.vertices[geometry.indices[i]];
            const auto& b=geometry.vertices[geometry.indices[i+1]];
            const auto& c=geometry.vertices[geometry.indices[i+2]];
            if(a.material!=0)continue;
            const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
            const float vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
            const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            // Backface culling must retain the exterior, matching shading.
            assert(nx*(a.nx+b.nx+c.nx)+ny*(a.ny+b.ny+c.ny)+nz*(a.nz+b.nz+c.nz)>0);
        }
        mesh.trunkFlare=false;
        const auto isolated=buildArborMesh(tree,mesh);
        assert(std::none_of(isolated.vertices.begin(),isolated.vertices.end(),
            [](const auto& v){return v.material==0&&v.y<0;}));
    }
    for(uint32_t species:{1u,2u,3u,4u,10u,16u}) {
        constexpr uint32_t variants=4;
        std::vector<bool> seen(species*variants,false);
        for(int64_t z=-20;z<=20;++z)for(int64_t x=-20;x<=20;++x) {
            const auto index=treePrototypeVariant(x,z,2718,species,variants);
            assert(index<seen.size());seen[index]=true;
            assert(index==treePrototypeVariant(x,z,2718,species,variants));
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
                if(!dx&&!dz)continue;
                const auto neighbor=treePrototypeVariant(x+dx,z+dz,2718,species,variants);
                assert(index!=neighbor);
                if(species>=4)assert(index/variants!=neighbor/variants);
            }
        }
        assert(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}));
    }
    {
        bool rejected=false;
        try{(void)treePrototypeVariant(0,0,1,1,3);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    {
        // A rounded pine crown widens above its base, then narrows at the
        // leader. The former monotonic envelope was just another cone.
        TreeProfile pine;
        pine.shape=CrownShape::Conifer;pine.minHeightCm=pine.maxHeightCm=1800;
        pine.crownStartPermille=400;pine.primaryBranches=21;
        pine.roundedConiferCrown=true;
        const auto tree=generateTree(pine,42);
        const auto reach=[](const TreeBranch& b) {
            return std::max(std::abs(b.x1-b.x0),std::abs(b.z1-b.z0));
        };
        assert(reach(tree.branches[11])>reach(tree.branches[1])*3);
        assert(reach(tree.branches[11])>reach(tree.branches[21])*3);
    }
    for(const auto& preset:botanicalTreePresets()) {
        const auto full=generateTree(preset.profile,42);
        sameTree(full,generateTree(preset.profile,42));validateGraph(full,preset.profile.roundedBroadleafCrown||preset.profile.branchDroopPermille>0);
        PlantEnvironment mature;mature.ageTicks=100;mature.matureTicks=100;
        PlantEnvironment young=mature;young.ageTicks=10;
        assert(treeProfileAtAge(preset,young).maxHeightCm<treeProfileAtAge(preset,mature).maxHeightCm);
        CanopyPhenology winter;winter.broadleafCoverPermille=0;
        assert(treeCanopyAtAge(preset,mature,winter)==(treeIsEvergreen(preset.profile)?1000u:0u));
        ArborMeshSettings mesh;mesh.doubleSidedLeaves=false;
        mesh.naturalBroadleaf=preset.profile.shape!=CrownShape::Conifer;
        mesh.naturalConifer=!mesh.naturalBroadleaf;
        const auto summer=buildArborMesh(full,mesh);
        mesh.leafCoveragePermille=0;const auto bare=buildArborMesh(full,mesh);
        assert(bare.indices.size()<summer.indices.size());
        assert(std::none_of(bare.vertices.begin(),bare.vertices.end(),[](const auto& v){return v.material==1;}));
        assert(!bare.indices.empty());
        for(size_t i=1;i<full.branches.size();++i) {
            const auto& child=full.branches[i];
            if(child.parent<=0)continue;
            const auto& parent=full.branches[size_t(child.parent)];
            // Integer interpolation stays on the parent's centreline to1cm.
            const double dx=parent.x1-parent.x0,dy=parent.y1-parent.y0,dz=parent.z1-parent.z0;
            const double length2=dx*dx+dy*dy+dz*dz;
            if(length2<1)continue;
            const double t=((child.x0-parent.x0)*dx+(child.y0-parent.y0)*dy+(child.z0-parent.z0)*dz)/length2;
            assert(t>=-.01&&t<=1.01);
            if(length2>=100)assert(t>.3&&t<.95);
            assert(std::hypot(child.x0-parent.x0-t*dx,child.z0-parent.z0-t*dz)<2);
        }
    }
    testTreeShapesAndLods();
    testInstancesAndIndependentWorkers();
    testInvalidProfiles();
    testGeneratedGeometry();
}
