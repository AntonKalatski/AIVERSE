#pragma once
#include "apps/editor/DocumentEntityIdentity.h"
namespace aiverse::studio {
inline std::string studioDocumentId(editor::DocumentEntityRef ref) { return editor::documentEntityId(ref); }
inline std::string studioDocumentId(const editor::MapDoc& map,editor::DocumentEntityRef ref) { return editor::documentEntityId(map,ref); }
}
