// Experimental byte-level delta for serialized Simulation snapshots.
// Do not confuse with tracked COW World/SimSnapshot rollback: this operates
// on complete serialized byte strings and requires a matching base snapshot.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::ecs_experiment {
class SerializedDelta {
    static constexpr size_t kPage = 256;
    struct Page {
        size_t index;
        std::string data;
    };
    uint64_t baseHash_=0, resultHash_=0;
    size_t resultSize_=0;
    bool full_=false;
    std::string replacement_;
    std::vector<Page> changed_;
    static uint64_t checksum(const std::string& bytes) {
        uint64_t hash=1469598103934665603ull;
        for(unsigned char b:bytes){hash^=b;hash*=1099511628211ull;}
        return hash;
    }
public:
    static SerializedDelta encode(const std::string& base,
                                  const std::string& current) {
        SerializedDelta delta;
        delta.baseHash_=checksum(base);
        delta.resultHash_=checksum(current);
        delta.resultSize_=current.size();
        size_t bytes=0;
        for(size_t offset=0;offset<current.size();offset+=kPage) {
            const size_t length=std::min(kPage,current.size()-offset);
            bool changed=false;
            if(offset+length>base.size())changed=true;
            else changed=base.compare(offset,length,current,offset,length)!=0;
            if(changed) {
                delta.changed_.push_back({offset/kPage,current.substr(offset,length)});
                bytes+=length+sizeof(size_t)*2;
            }
        }
        // Compressing all pages is counterproductive. Preserve a complete
        // replacement if page metadata/payload exceeds source size.
        if(bytes>=current.size() && !current.empty()) {
            delta.changed_.clear();
            delta.full_=true;
            delta.replacement_=current;
        }
        return delta;
    }
    std::string restore(const std::string& base) const {
        if(checksum(base)!=baseHash_)throw std::runtime_error("snapshot base checksum mismatch");
        if(full_) {
            if(checksum(replacement_)!=resultHash_)throw std::runtime_error("snapshot replacement checksum mismatch");
            return replacement_;
        }
        std::string result=base;
        result.resize(resultSize_,char(0));
        for(const auto& page:changed_) {
            const size_t offset=page.index*kPage;
            if(offset>result.size() || page.data.size()>result.size()-offset)
                throw std::runtime_error("snapshot page outside result");
            std::copy(page.data.begin(),page.data.end(),result.begin()+offset);
        }
        if(checksum(result)!=resultHash_)throw std::runtime_error("snapshot result checksum mismatch");
        return result;
    }
    size_t storedBytes() const {
        if(full_)return replacement_.size();
        size_t bytes=0;
        for(const auto& page:changed_)bytes+=page.data.size()+2*sizeof(size_t);
        return bytes;
    }
    size_t changedPages() const {return changed_.size();}
    bool isFull() const {return full_;}
};
} // namespace aiverse::ecs_experiment
