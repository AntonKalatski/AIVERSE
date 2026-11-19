#include "StrataDiskPages.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace aiverse::render::strata {
namespace {
using Key=std::pair<uint32_t,uint32_t>;
constexpr char kMagic[8]={'S','T','R','I','D','X','0','3'};
constexpr uint32_t kVersion=1;
constexpr uint64_t kHeaderSize=40, kEntrySize=32;
constexpr uint64_t kMaximumFileSize=256ull*1024ull*1024ull;
constexpr uint32_t kMaximumPages=2000000;
bool fail(std::string* err,const char* reason) {
    if(err)*err=reason;
    return false;
}
void put32(std::ostream& out,uint32_t value) {
    for(unsigned i=0;i<4;++i)out.put(char((value>>(i*8))&255u));
}
void put64(std::ostream& out,uint64_t value) {
    for(unsigned i=0;i<8;++i)out.put(char((value>>(i*8))&255u));
}
bool get32(std::istream& in,uint32_t& value) {
    value=0;
    for(unsigned i=0;i<4;++i) {
        const int b=in.get();
        if(b==std::char_traits<char>::eof())return false;
        value |= uint32_t(uint8_t(b))<<(i*8);
    }
    return true;
}
bool get64(std::istream& in,uint64_t& value) {
    value=0;
    for(unsigned i=0;i<8;++i) {
        const int b=in.get();
        if(b==std::char_traits<char>::eof())return false;
        value |= uint64_t(uint8_t(b))<<(i*8);
    }
    return true;
}
void mix(uint64_t& hash,uint32_t value) {
    for(unsigned k=0;k<4;++k){
        hash^=uint8_t(value>>(8*k));
        hash*=1099511628211ull;
    }
}
uint64_t hashIndices(const uint32_t* data,size_t count) {
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<count;++i)mix(hash,data[i]);
    return hash;
}
uint64_t signature(const ModelAsset& asset) {
    uint64_t hash=14695981039346656037ull;
    mix(hash,uint32_t(asset.primitives.size()));
    for(const auto& entry:asset.primitives){
        mix(hash,entry.mesh);mix(hash,entry.primitive);mix(hash,entry.material);
        const auto& g=entry.geometry;
        mix(hash,uint32_t(g.nodes.size()));
        mix(hash,uint32_t(g.indices.size()));
        mix(hash,uint32_t(g.roots.size()));
        for(uint32_t root:g.roots)mix(hash,root);
        for(const auto& n:g.nodes){
            mix(hash,n.firstChild);mix(hash,n.childCount);
            mix(hash,n.firstIndex);mix(hash,n.indexCount);
            mix(hash,n.materialGroup);
            uint32_t bits=0;
            std::memcpy(&bits,&n.geometricError,sizeof(bits));
            mix(hash,bits);
        }
        for(uint32_t index:g.indices)mix(hash,index);
    }
    return hash;
}
struct Entry {
    uint32_t count=0;
    uint64_t offset=0, checksum=0;
};
bool readPage(const std::string& path,const Entry& entry,
              std::vector<uint32_t>& out) {
    std::ifstream file(path,std::ios::binary);
    if(!file || entry.count==0)return false;
    file.seekg(std::streamoff(entry.offset),std::ios::beg);
    if(!file)return false;
    std::vector<uint32_t> page(entry.count);
    for(uint32_t& index:page)
        if(!get32(file,index))return false;
    if(hashIndices(page.data(),page.size())!=entry.checksum)
        return false;
    out=std::move(page);
    return true;
}
bool sourceValid(const ModelAsset& asset) {
    if(asset.primitives.empty() || !asset.skipped.empty())return false;
    for(const auto& entry:asset.primitives){
        const auto& g=entry.geometry;
        if(!validate(g.nodes,g.roots) ||
           g.bounds.size()!=g.nodes.size() || g.indices.empty())
            return false;
        for(size_t n=0;n<g.nodes.size();++n) {
            const auto& node=g.nodes[n];
            if(!node.resident ||
               node.firstIndex>g.indices.size() ||
               node.indexCount>g.indices.size()-node.firstIndex)
                return false;
            const auto& b=g.bounds[n];
            for(unsigned k=0;k<3;++k)
                if(!std::isfinite(b.min[k]) || !std::isfinite(b.max[k]) ||
                   b.min[k]>b.max[k])
                    return false;
        }
    }
    return true;
}
} // namespace

struct DiskIndexPages::State {
    std::string path;
    std::map<Key,Entry> entries;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<Key> queue;
    std::set<Key> pending;
    std::set<Key> desired;
    std::set<Key> failures;
    std::map<Key,std::vector<uint32_t>> pages;
    size_t readyLimit=0, queuedLimit=0, readyBytes=0;
    bool stop=false;
    std::thread worker;
    DiskPageStats counters;
};

DiskIndexPages::DiskIndexPages()=default;
DiskIndexPages::~DiskIndexPages(){close();}

std::string DiskIndexPages::archiveFilename(const std::string& sourceKey) {
    uint64_t h=14695981039346656037ull;
    for(unsigned char ch:sourceKey){
        h^=ch;
        h*=1099511628211ull;
    }
    std::ostringstream result;
    result<<"strata-"<<std::hex<<std::setw(16)<<std::setfill('0')<<h<<".strataidx";
    return result.str();
}

bool DiskIndexPages::writeArchive(const std::string& path,
                                  const ModelAsset& asset,std::string* error) {
    if(error)error->clear();
    if(path.empty() || !sourceValid(asset))
        return fail(error,"STRATA disk archive requires valid staged asset");
    std::vector<std::pair<Key,Entry>> directory;
    for(uint32_t p=0;p<asset.primitives.size();++p){
        const auto& g=asset.primitives[p].geometry;
        for(uint32_t n=0;n<g.nodes.size();++n){
            const auto& node=g.nodes[n];
            directory.push_back({{p,n},{node.indexCount,0,
                hashIndices(g.indices.data()+node.firstIndex,node.indexCount)}});
            if(directory.size()>kMaximumPages)
                return fail(error,"STRATA archive too many pages");
        }
    }
    uint64_t cursor=kHeaderSize + directory.size()*kEntrySize;
    for(auto& entry:directory){
        entry.second.offset=cursor;
        cursor+=uint64_t(entry.second.count)*sizeof(uint32_t);
        if(cursor>kMaximumFileSize)
            return fail(error,"STRATA archive exceeds maximum size");
    }
    const std::filesystem::path destination(path);
    const std::filesystem::path temporary=destination.string()+".tmp";
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    if(!file)return fail(error,"STRATA archive output cannot be created");
    file.write(kMagic,8);
    put32(file,kVersion);
    put32(file,uint32_t(directory.size()));
    put64(file,signature(asset));
    put64(file,kHeaderSize);
    put64(file,kHeaderSize+uint64_t(directory.size())*kEntrySize);
    for(const auto& [key,entry]:directory){
        put32(file,key.first);put32(file,key.second);
        put32(file,entry.count);put32(file,0);
        put64(file,entry.offset);put64(file,entry.checksum);
    }
    for(const auto& [key,entry]:directory){
        const auto& g=asset.primitives[key.first].geometry;
        const uint32_t from=g.nodes[key.second].firstIndex;
        for(uint32_t n=0;n<entry.count;++n)
            put32(file,g.indices[from+n]);
    }
    file.flush();
    const bool good=bool(file);
    file.close();
    if(!good){
        std::error_code ec;
        std::filesystem::remove(temporary,ec);
        return fail(error,"STRATA disk archive write failed");
    }
    std::error_code ec;
    std::filesystem::rename(temporary,destination,ec);
    if(ec){
        std::filesystem::remove(temporary,ec);
        return fail(error,"STRATA disk archive atomic rename failed");
    }
    return true;
}

bool DiskIndexPages::open(const std::string& path,const ModelAsset& asset,
                          size_t readyBudgetBytes,size_t maxRequests,
                          std::string* error) {
    if(error)error->clear();
    close();
#if defined(__EMSCRIPTEN__)
    (void)path;(void)asset;(void)readyBudgetBytes;(void)maxRequests;
    return fail(error,"STRATA disk IO is unsupported on WebGL2");
#else
    if(!sourceValid(asset) || !readyBudgetBytes || !maxRequests ||
       readyBudgetBytes>kMaximumFileSize || maxRequests>kMaximumPages)
        return fail(error,"STRATA disk archive invalid limits or source");
    std::error_code ec;
    const uint64_t size=std::filesystem::file_size(path,ec);
    if(ec || size<kHeaderSize || size>kMaximumFileSize)
        return fail(error,"STRATA archive missing or oversized");
    std::ifstream file(path,std::ios::binary);
    std::array<char,8> magic{};
    file.read(magic.data(),8);
    uint32_t version=0,count=0;
    uint64_t sourceHash=0,dirOffset=0,dataOffset=0;
    if(!file || magic!=std::array<char,8>{kMagic[0],kMagic[1],kMagic[2],
                                           kMagic[3],kMagic[4],kMagic[5],
                                           kMagic[6],kMagic[7]} ||
       !get32(file,version) || !get32(file,count) ||
       !get64(file,sourceHash) || !get64(file,dirOffset) ||
       !get64(file,dataOffset) || version!=kVersion ||
       !count || count>kMaximumPages ||
       sourceHash!=signature(asset) || dirOffset!=kHeaderSize ||
       dataOffset!=kHeaderSize+uint64_t(count)*kEntrySize ||
       dataOffset>size)
        return fail(error,"STRATA archive header/signature is invalid");

    auto next=std::make_unique<State>();
    next->path=path;
    next->readyLimit=readyBudgetBytes;
    next->queuedLimit=maxRequests;
    uint64_t maxSiblingBytes=0,expectedPages=0;
    for(uint32_t p=0;p<asset.primitives.size();++p){
        const auto& nodes=asset.primitives[p].geometry.nodes;
        expectedPages+=nodes.size();
        for(const auto& parent:nodes){
            uint64_t total=0;
            for(uint32_t child=0;child<parent.childCount;++child)
                total+=uint64_t(nodes[parent.firstChild+child].indexCount)*4u;
            maxSiblingBytes=std::max(maxSiblingBytes,total);
        }
    }
    if(expectedPages!=count || readyBudgetBytes<maxSiblingBytes)
        return fail(error,"STRATA disk staging budget smaller than sibling group");

    uint64_t previousEnd=dataOffset;
    for(uint32_t i=0;i<count;++i){
        uint32_t p=0,n=0,indices=0,reserved=0;
        uint64_t offset=0,hash=0;
        if(!get32(file,p)||!get32(file,n)||!get32(file,indices)||
           !get32(file,reserved)||!get64(file,offset)||
           !get64(file,hash)||p>=asset.primitives.size() ||
           n>=asset.primitives[p].geometry.nodes.size() ||
           indices!=asset.primitives[p].geometry.nodes[n].indexCount ||
           reserved || offset!=previousEnd || offset>size ||
           uint64_t(indices)*4u>size-offset ||
           !next->entries.emplace(Key{p,n},Entry{indices,offset,hash}).second)
            return fail(error,"STRATA archive directory is corrupt");
        previousEnd=offset+uint64_t(indices)*4u;
    }
    if(next->entries.size()!=expectedPages || previousEnd!=size)
        return fail(error,"STRATA archive payload length mismatch");

    // Worker owns State only while DiskIndexPages owns it; close() signals,
    // wakes and joins before State, ModelAsset or scene can be destroyed.
    State* state=next.get();
    state->worker=std::thread([state]() {
        for(;;) {
            Key key;
            Entry entry;
            {
                std::unique_lock lock(state->mutex);
                state->cv.wait(lock,[&]{return state->stop||!state->queue.empty();});
                if(state->stop)return;
                key=state->queue.front();
                state->queue.pop_front();
                entry=state->entries.at(key);
                if(!state->desired.count(key)){
                    state->pending.erase(key);
                    continue;
                }
                // Avoid reading an unbounded number of ready pages when
                // the render thread cannot consume them this frame.
                const size_t bytes=size_t(entry.count)*4u;
                state->cv.wait(lock,[&]{
                    return state->stop || !state->desired.count(key) ||
                        bytes<=state->readyLimit-state->readyBytes;
                });
                if(state->stop)return;
                if(!state->desired.count(key)){
                    state->pending.erase(key);
                    continue;
                }
            }
            std::vector<uint32_t> data;
            const bool good=readPage(state->path,entry,data);
            {
                std::lock_guard lock(state->mutex);
                state->pending.erase(key);
                if(good){
                    ++state->counters.completedReads;
                    state->counters.bytesRead+=uint64_t(entry.count)*4u;
                    if(state->desired.count(key)){
                        state->readyBytes+=data.size()*4u;
                        state->pages[key]=std::move(data);
                    }
                }else{
                    state->failures.insert(key);
                    ++state->counters.failedReads;
                }
            }
            state->cv.notify_all();
        }
    });
    state_=std::move(next);
    return true;
#endif
}

bool DiskIndexPages::readRoot(uint32_t primitive,uint32_t node,
                              std::vector<uint32_t>& result,
                              std::string* error) {
    if(error)error->clear();
    if(!state_)return fail(error,"STRATA disk archive not open");
    const auto it=state_->entries.find({primitive,node});
    if(it==state_->entries.end())
        return fail(error,"STRATA root page not in archive");
    std::vector<uint32_t> data;
    if(!readPage(state_->path,it->second,data)){
        std::lock_guard lock(state_->mutex);
        ++state_->counters.failedReads;
        return fail(error,"STRATA pinned root page failed integrity check");
    }
    {
        std::lock_guard lock(state_->mutex);
        ++state_->counters.completedReads;
        state_->counters.bytesRead+=uint64_t(data.size())*sizeof(uint32_t);
    }
    result=std::move(data);
    return true;
}

void DiskIndexPages::retainRequested(
        const std::vector<std::pair<uint32_t,uint32_t>>& wanted) {
    if(!state_)return;
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    s.desired={wanted.begin(),wanted.end()};
    // An already running worker cannot be interrupted mid-read. Its output
    // is discarded if the source is no longer in the desired set.
    for(auto it=s.queue.begin();it!=s.queue.end();){
        if(!s.desired.count(*it)){
            s.pending.erase(*it);
            it=s.queue.erase(it);
        }else ++it;
    }
    for(auto it=s.pages.begin();it!=s.pages.end();){
        if(!s.desired.count(it->first)){
            s.readyBytes-=it->second.size()*4u;
            it=s.pages.erase(it);
        }else ++it;
    }
    s.cv.notify_all();
}

bool DiskIndexPages::request(uint32_t primitive,uint32_t node) {
    if(!state_)return false;
    auto& s=*state_;
    const Key key{primitive,node};
    std::lock_guard lock(s.mutex);
    const auto it=s.entries.find(key);
    if(s.stop||it==s.entries.end()||s.failures.count(key)||
       !s.desired.count(key))return false;
    if(s.pages.count(key)||s.pending.count(key))return true;
    if(s.pending.size()>=s.queuedLimit){
        ++s.counters.queueRefused;
        return false;
    }
    s.pending.insert(key);
    s.queue.push_back(key);
    ++s.counters.requests;
    s.cv.notify_one();
    return true;
}

bool DiskIndexPages::ready(uint32_t primitive,uint32_t node,
                           std::vector<uint32_t>& out) const {
    if(!state_)return false;
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    const auto it=s.pages.find({primitive,node});
    if(it==s.pages.end())return false;
    out=it->second;
    return true;
}
void DiskIndexPages::consume(uint32_t primitive,uint32_t node) {
    if(!state_)return;
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    const auto it=s.pages.find({primitive,node});
    if(it==s.pages.end())return;
    s.readyBytes-=it->second.size()*4u;
    s.pages.erase(it);
    s.cv.notify_all();
}
bool DiskIndexPages::failed(uint32_t primitive,uint32_t node) const {
    if(!state_)return true;
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    return s.failures.count({primitive,node})!=0;
}
DiskPageStats DiskIndexPages::stats() const {
    if(!state_)return {};
    auto& s=*state_;
    std::lock_guard lock(s.mutex);
    auto result=s.counters;
    result.queued=s.pending.size();
    result.readyBytes=s.readyBytes;
    return result;
}
bool DiskIndexPages::valid() const {return bool(state_);}
void DiskIndexPages::close() {
    if(!state_)return;
    {
        std::lock_guard lock(state_->mutex);
        state_->stop=true;
        state_->cv.notify_all();
    }
    if(state_->worker.joinable())state_->worker.join();
    state_.reset();
}
bool SceneStaging::writeDiskArchives(
        const std::string& directory,
        std::vector<std::string>* failures) const {
    if(failures)failures->clear();
    if(directory.empty())return false;
    std::error_code ec;
    std::filesystem::create_directories(directory,ec);
    if(ec)return false;
    bool success=true;
    for(const auto& [key,asset]:assets_) {
        const auto path=(std::filesystem::path(directory) /
            DiskIndexPages::archiveFilename(key)).string();
        std::string reason;
        if(!DiskIndexPages::writeArchive(path,asset,&reason)) {
            success=false;
            if(failures)
                failures->push_back(key+": "+reason);
        }
    }
    return success;
}
} // namespace aiverse::render::strata
