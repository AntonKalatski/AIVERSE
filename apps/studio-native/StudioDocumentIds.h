#pragma once
#include "apps/editor/DocumentEntityIdentity.h"
namespace aiverse::studio {
inline std::string studioDocumentId(editor::DocumentEntityRef ref) { return editor::documentEntityId(ref); }
inline std::string studioDocumentId(const editor::MapDoc& map,editor::DocumentEntityRef ref) { return editor::documentEntityId(map,ref); }
inline std::string studioDocumentId(editor::DocumentEntityRef ref,const editor::MapDoc& map) { return editor::documentEntityId(map,ref); }
inline int studioMapEntityIndex(const std::string& id,const editor::MapDoc& map) {
    if(id.rfind("entity:@",0)==0){const int i=map.findEntity(id.substr(8));return i>=0&&editor::authoredEntity(map.data.entities[size_t(i)])?i:-1;}
    if(id.rfind("entity:",0)!=0||id.size()==7)return -1;
    size_t n=0;for(size_t i=7;i<id.size();++i){const char c=id[i];if(c<'0'||c>'9'||n>size_t(INT32_MAX)/10)return -1;n=n*10+size_t(c-'0');if(n>size_t(INT32_MAX))return -1;}
    return n<map.data.entities.size()?int(n):-1;
}
}
