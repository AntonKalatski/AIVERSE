// Stable source KIND identity for authoring document objects; ordinals are
// still provisional until the editor's documents gain stable IDs.
#pragma once
#include "apps/editor/EditorEntityAdapter.h"
#include <string>

namespace aiverse::studio {
inline std::string studioDocumentId(editor::DocumentEntityRef ref) {
    switch (ref.source) {
        case editor::DocumentEntityRef::Source::MapEntity:
            return "entity:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsCollider:
            return "collider:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsSpawn:
            return "spawn:" + std::to_string(ref.index);
    }
    return {};
}
inline std::string studioDocumentId(editor::DocumentEntityRef ref,const editor::MapDoc& map) {
    const auto* e=editor::documentMapEntity(map,ref);
    if(e && editor::authoredEntity(*e) && !e->id.empty())return "entity:@"+e->id;
    return studioDocumentId(ref);
}
inline int studioMapEntityIndex(const std::string& id,const editor::MapDoc& map) {
    if(id.rfind("entity:@",0)==0) {
        const int index=map.findEntity(id.substr(8));
        return index>=0 && editor::authoredEntity(map.data.entities[size_t(index)]) ? index : -1;
    }
    if(id.rfind("entity:",0)!=0 || id.size()==7)return -1;
    size_t value=0;
    for(size_t i=7;i<id.size();++i){const auto c=id[i];if(c<'0'||c>'9'||value>size_t(INT32_MAX)/10)return -1;value=value*10+size_t(c-'0');if(value>size_t(INT32_MAX))return -1;}
    return value<map.data.entities.size()?int(value):-1;
}
} // namespace aiverse::studio
