// Stage 108 deferred: explicit device generation prevents false validity
// when a GPU Device address is reused after host-controlled shutdown.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "engine/render/RenderDeviceLifetime.h"
#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__) || defined(__unix__)
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#endif

// Unlike CHECK(), CHECK is active in Release and RelWithDebInfo CTest runs.
[[noreturn]] static void testCheckFailed(const char* expr,const char* file,int line) {
    std::fprintf(stderr,"render_device_epoch CHECK failed: %s at %s:%d\n",
                 expr,file,line);
    std::abort();
}
#define CHECK(expr) do {if(!(expr))testCheckFailed(#expr,__FILE__,__LINE__);} while(false)
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
using namespace aiverse;
class StubDevice final: public gfx::Device {
public:
    gfx::DeviceInfo d{};
    std::string error;
    const gfx::DeviceInfo& info() const override{return d;}
    const std::string& lastError() const override{return error;}
    bool waitForResourceIdle() override{return true;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override{}
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};
// CPU-only regressions for Stage 129-131 unique-owned Model cache leases.
// These do not assert GPU completion: StubDevice has no in-flight work.
static void verifyReadLeaseContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto model=std::make_unique<render::Model>();
    model->path="models/hero.glb";
    const render::Model* resident=cache.insertIfAbsent(
        "models/hero.glb#geometry",std::move(model));
    CHECK(resident);
    CHECK(!cache.acquireModelRead("missing.glb"));
    CHECK(!cache.acquireModelRead(static_cast<const render::Model*>(nullptr)));
    render::Model foreign;
    CHECK(!cache.acquireModelRead(&foreign));

    auto narrow=cache.acquireModelRead(resident);
    auto byKey=cache.acquireModelRead("models/hero.glb#geometry");
    CHECK(narrow && narrow.get()==resident && byKey.get()==resident);
    auto moved=std::move(narrow);
    CHECK(!narrow && moved.get()==resident);
    auto frame=cache.acquireFrameReadScope();
    CHECK(frame.valid() && cache.activeModelReaders()==3);
    const auto first=cache.modelReadSafetyStats();
    CHECK(first.peakReaders>=3);

    // A foreign thread MUST be rejected before it even reads models_.
    bool threadDenied=false;
    std::thread foreignLookup([&]{
        try {auto candidate=cache.acquireModelRead("models/hero.glb#geometry");(void)candidate;}
        catch(const std::logic_error&){threadDenied=true;}
    });
    foreignLookup.join();
    CHECK(threadDenied);

    unsigned callbacks=0;
    auto detach=[&](const auto&){++callbacks;};
    auto release=[&](const render::Model*){++callbacks;};
    auto denied=[&](auto&& fn){
        bool thrown=false;
        try{fn();}
        catch(const std::logic_error&){thrown=true;}
        CHECK(thrown);
    };
    denied([&]{cache.replace(dev,"models/hero.glb#geometry",
        std::make_unique<render::Model>(),release);});
    denied([&]{cache.invalidateSources(dev,
        std::vector<std::string>{"models/hero.glb"},detach);});
    denied([&]{cache.trimUnused(dev,{},release);});
    denied([&]{cache.shutdown(dev);});
    CHECK(callbacks==0);
    CHECK(cache.find("models/hero.glb#geometry")==resident);
    CHECK(cache.activeModelReaders()==3);
    CHECK(cache.modelReadSafetyStats().blockedMutations==4);
    CHECK(cache.modelReadSafetyStats().peakReaders>=3);

    // Streaming publication of NEW keys is deliberately allowed under a
    // frame reader: std::map node insertion cannot move existing Model*.
    cache.insertIfAbsent("models/new.glb",
                         std::make_unique<render::Model>());
    render::ModelResourceCache::ModelMap staged;
    staged.emplace("models/streamed.glb",
                   std::make_unique<render::Model>());
    cache.publishStagedModels(dev,staged);
    CHECK(staged.empty() && cache.find("models/new.glb") &&
           cache.find("models/streamed.glb"));
    CHECK(cache.activeModelReaders()==3);
    // Reject any staged-key collision atomically BEFORE extracting the
    // first map node; both the resident and staged data survive unchanged.
    render::ModelResourceCache::ModelMap collisions;
    collisions.emplace("models/another.glb",std::make_unique<render::Model>());
    collisions.emplace("models/hero.glb#geometry",std::make_unique<render::Model>());
    const auto beforeCollisionGeneration=cache.generation();
    bool collisionDenied=false;
    try {cache.publishStagedModels(dev,collisions);}
    catch(const std::logic_error&){collisionDenied=true;}
    CHECK(collisionDenied);
    CHECK(collisions.size()==2);
    CHECK(cache.generation()==beforeCollisionGeneration);
    CHECK(cache.find("models/hero.glb#geometry")==resident);
    CHECK(cache.find("models/another.glb")==nullptr);
    // The cache's std::map may not be written from a different thread,
    // even for a new key and even while coarse frame readers are live.
    bool foreignInsertDenied=false;
    std::thread foreignInsert([&]{
        try {
            cache.insertIfAbsent("models/foreign.glb",
                                 std::make_unique<render::Model>());
        }catch(const std::logic_error&){foreignInsertDenied=true;}
    });
    foreignInsert.join();
    CHECK(foreignInsertDenied);
    CHECK(!cache.contains("models/foreign.glb"));
    // Replacements and evictions remain forbidden until the last reader.

    frame.reset();
    moved.reset();
    byKey.reset();
    CHECK(cache.activeModelReaders()==0);
    cache.shutdown(dev); // no GPU buffers; empty/idle StubDevice
    CHECK(cache.find("models/hero.glb#geometry")==nullptr);
}
static void verifyReadOwnerRebind() {
    // The same CPU cache can be rebound on a DIFFERENT owner thread after
    // successful shutdown. No real GPU backend is accessed concurrently.
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    {auto frame=cache.acquireFrameReadScope();CHECK(frame);}
    cache.shutdown(dev);
    std::thread newOwner([&]{
        cache.bindResourceDevice(dev);
        auto frame=cache.acquireFrameReadScope();
        CHECK(frame.valid());
        frame.reset();
        cache.shutdown(dev);
    });
    newOwner.join();
    CHECK(cache.activeModelReaders()==0);
}

// Fatal owner-thread contracts must be verified in a separate process:
// they intentionally call std::terminate, never a catchable exception.
static int fatalChild(const std::string& mode) {
    render::ModelResourceCache cache;
    if(mode=="--fatal-lease") {
        auto* resident=cache.insertIfAbsent(
            "test",std::make_unique<render::Model>());
        auto token=cache.acquireModelRead(resident);
        std::thread foreign([guard=std::move(token)]() mutable {guard.reset();});
        foreign.join();
        return 0; // reaching this line means the required termination failed
    }
    if(mode=="--fatal-scope") {
        auto guard=cache.acquireFrameReadScope();
        std::thread foreign([guard=std::move(guard)]() mutable {guard.reset();});
        foreign.join();
        return 0;
    }
    if(mode=="--fatal-destroy") {
        auto holder=std::make_unique<render::ModelResourceCache>();
        auto guard=holder->acquireFrameReadScope();
        holder.reset(); // MUST terminate with a live reader
        guard.reset();
        return 0;
    }
    return 2;
}
#if defined(__APPLE__) || defined(__unix__)
static void verifyFatalReadContracts(const char* self) {
    for(const char* mode:{"--fatal-lease","--fatal-scope","--fatal-destroy"}) {
        pid_t pid=-1;
        char* args[]={const_cast<char*>(self),const_cast<char*>(mode),nullptr};
        char* env[]={nullptr};
        const int spawned=posix_spawnp(&pid,self,nullptr,nullptr,args,env);
        CHECK(spawned==0);
        int status=0;
        CHECK(waitpid(pid,&status,0)==pid);
        CHECK(WIFSIGNALED(status));
        CHECK(WTERMSIG(status)==SIGABRT);
    }
}
#endif

int main(int argc,char** argv) {
    if(argc==2)return fatalChild(argv[1]);
    CHECK(argc==1);
    verifyReadLeaseContracts();
    verifyReadOwnerRebind();
#if defined(__APPLE__) || defined(__unix__)
    verifyFatalReadContracts(argv[0]);
#endif
    StubDevice device;
    render::ModelResourceCache cache;
    render::ModelResourceCache hosted; // embedded GameClient shares editor epoch
    render::RenderDeviceLifetime first(device);
    const auto prior=first.lease();
    cache.bindResourceDevice(device,prior);
    hosted.bindResourceDevice(device,prior);
    CHECK(cache.resourceDeviceGeneration()==prior.generation());
    CHECK(hosted.resourceDeviceGeneration()==prior.generation());
    CHECK(prior.validFor(device));
    {
        render::RenderResourceOwnerGroup group;
        auto lease=group.registerScoped(prior,cache);
        auto hostedLease=group.registerScoped(prior,hosted);
        CHECK(group.snapshot(device).uniqueOwners==2);
        hostedLease.reset();
        lease.reset();
    }
    // Successful explicit GPU idle is required BEFORE retiring the
    // host's generation token or recreating the GPU device at this address.
    // Hosted GameClient destroys its resources first, then the owning
    // editor cache, before the ONE shared GPU generation is retired.
    hosted.shutdown(device);
    cache.shutdown(device);
    first.retire();
    CHECK(!prior.validFor(device));
    render::RenderDeviceLifetime next(device); // same pointer, new epoch
    const auto current=next.lease();
    CHECK(current.validFor(device));
    CHECK(current.generation()!=prior.generation());
    cache.bindResourceDevice(device,current);
    hosted.bindResourceDevice(device,current);
    CHECK(cache.resourceDeviceGeneration()==current.generation());
    CHECK(hosted.resourceDeviceGeneration()==current.generation());
    bool rejected=false;
    try {cache.bindResourceDevice(device,prior);}
    catch(const std::logic_error&) {rejected=true;}
    CHECK(rejected);
    hosted.shutdown(device);
    cache.shutdown(device);
}
