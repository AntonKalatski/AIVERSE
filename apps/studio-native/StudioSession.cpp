#include "StudioSession.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/editor/EditorSceneNodes.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include "apps/studio-native/StudioLightingValidation.h"
#include "apps/studio-native/StudioFpsLightingDocument.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "apps/studio-native/StudioShaderGraphValidation.h"
#include "apps/studio-native/StudioShaderGraphDraft.h"
#include "apps/studio-native/StudioFacialRigDocument.h"
#include <algorithm>
#include "apps/editor/core/MaterialOps.h"
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aiverse::studio {
namespace {
Json str(const std::string& s) { return Json::string(s); }
Json num(int64_t n) { return Json::number(n); }
Json prop(const char* key, const char* label, const char* type, Json value, bool editable = true) {
    Json r = Json::object();
    r.set("key", str(key)); r.set("label", str(label));
    r.set("type", str(type)); r.set("value", std::move(value));
    r.set("editable", Json::fromBool(editable));
    return r;
}
Json numericProp(const char* key, const char* label, Json value, bool integer = false) {
    Json r = prop(key, label, "number", std::move(value));
    const bool owner = std::string(key) == "owner";
    r.set("min", Json::number(owner ? int64_t(-1) : int64_t(-1000000)));
    r.set("max", Json::number(owner ? int64_t(10000) : int64_t(1000000)));
    r.set("step", Json::number(owner ? int64_t(1) : int64_t(0))); // overwritten below for floating fields
    if (!owner) r.set("step", Json::number(0.1, 3));
    if (integer) r.set("integer", Json::fromBool(true));
    return r;
}
std::string nodeId(const editor::DocumentEntityRef& ref) { return studioDocumentId(ref); }
}
StudioSession::StudioSession(const std::string& engineRoot, const std::string& project) {
    ws_.engineDir = engineRoot;
    if (!project.empty()) {
        std::string error;
        if (!ws_.openGame(project, &error)) throw std::runtime_error(error.empty() ? "Cannot open project" : error);
        // A project opened from the CLI follows the same content policy as
        // projects chosen through the native Open Project panel.
        (void)studioContentRoot(ws_.project.dir);
    }
}
int StudioSession::indexFor(const std::string& id) const {
    if (id.compare(0, 7, "entity:") != 0 || id.size() == 7) throw std::runtime_error("Invalid entity ID");
    uint64_t n = 0;
    for (size_t i = 7; i < id.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(id[i]);
        if (!std::isdigit(c)) throw std::runtime_error("Invalid entity ID");
        const uint64_t digit = uint64_t(c - '0');
        const uint64_t limit = uint64_t(std::numeric_limits<int>::max());
        if (n > (limit - digit) / 10) throw std::runtime_error("Entity index overflow");
        n = n * 10 + digit;
    }
    if (n >= ws_.map.data.entities.size()) throw std::runtime_error("Entity does not exist");
    return int(n);
}
Json StudioSession::propertiesFor(const std::string& entityId) const {
    if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
    Json fields = Json::array();
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map, ws_.project.isFps())) {
        if (nodeId(node.ref) != entityId) continue;
        if (node.ref.source == editor::DocumentEntityRef::Source::MapEntity) {
            const auto& e = ws_.map.data.entities.at(node.ref.index);
            fields.push(prop("type", "Type", "text", str(e.type)));
            fields.push(numericProp("x", "Position X", Json::number(double(e.x), 4)));
            fields.push(numericProp("z", "Position Z", Json::number(double(e.z), 4)));
            fields.push(numericProp("owner", "Owner", num(int64_t(e.owner)), true));
        } else if (node.hasPosition) {
            fields.push(numericProp("x", "Position X", Json::number(node.position.x, 3)));
            fields.push(numericProp("y", "Position Y", Json::number(node.position.y, 3)));
            fields.push(numericProp("z", "Position Z", Json::number(node.position.z, 3)));
        }
        return fields;
    }
    throw std::runtime_error("Unknown document object");
}
Json StudioSession::snapshot() const {
    Json result = Json::object();
    Json project = Json::object();
    project.set("name", str(ws_.project.manifest.getStr("name", "No project opened")));
    project.set("path", str(ws_.project.dir));
    project.set("dirty", Json::fromBool(ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty));
    result.set("project", std::move(project));
    Json scene = Json::object();
    scene.set("name", str(ws_.map.data.name));
    Json rows = Json::array();
    const auto nodes = editor::enumerateDocumentSceneNodes(ws_.map, ws_.project.isFps());
    for (const auto& node : nodes) {
        Json row = Json::object();
        row.set("id", str(nodeId(node.ref)));
        row.set("name", str(node.label));
        // UI receives only generic display kind; document source names remain native.
        row.set("kind", str(node.ref.source == editor::DocumentEntityRef::Source::MapEntity
            ? (node.ref.index < ws_.map.data.entities.size() ? ws_.map.data.entities[node.ref.index].type : "Entity")
            : node.ref.source == editor::DocumentEntityRef::Source::FpsCollider ? "Collider" : "Spawn"));
        row.set("visible", Json::fromBool(true));
        rows.push(std::move(row));
    }
    scene.set("entities", std::move(rows));
    result.set("scene", std::move(scene));
    const bool valid = !selected_.empty() && std::any_of(nodes.begin(), nodes.end(),
        [&](const auto& node) { return nodeId(node.ref) == selected_; });
    result.set("selectedId", valid ? str(selected_) : Json());
    result.set("properties", valid ? propertiesFor(selected_) : Json::array());
    Json logs = Json::array();
    const auto& activity = ws_.activity;
    for (size_t i = activity.size() > 100 ? activity.size() - 100 : 0; i < activity.size(); ++i) {
        Json row = Json::object();
        row.set("level", str(activity[i].error ? "error" : "info"));
        row.set("text", str(activity[i].text));
        row.set("time", str(activity[i].source));
        logs.push(std::move(row));
    }
    result.set("logs", std::move(logs));
    Json history = Json::object();
    history.set("canUndo", Json::fromBool(ws_.map.canUndo()));
    history.set("canRedo", Json::fromBool(ws_.map.canRedo()));
    result.set("history", std::move(history));
    result.set("mode", str("edit"));
    result.set("previewMaterial", previewMaterialName_.empty() ? Json() : str(previewMaterialName_));
    return result;
}
void StudioSession::refreshMaterialPreview() {
    if (previewMaterialName_.empty()) {
        previewMaterialFactors_ = Json();
        ++previewMaterialRevision_;
        return;
    }
    Json request=Json::object();
    request.set("name",str(previewMaterialName_));
    const Json result=ws_.call("material_get",request,"studio");
    if(result.has("error")) throw std::runtime_error(result.getStr("error"));
    const Json& resolved=result["resolved"];
    if(!resolved.isObject()) throw std::runtime_error("Resolved PBR preview material missing");
    previewMaterialFactors_=resolved;
    ++previewMaterialRevision_;
}
Json StudioSession::openProject(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Project path is empty");
    std::string error;
    if (!ws_.openGame(path, &error))
        throw std::runtime_error(error.empty() ? "Unable to open project" : error);
    (void)studioContentRoot(ws_.project.dir);
    pendingAssetTransfer_.reset();
    selected_.clear();
    facialPreview_.reset(); // project identity may change; never reuse prior GUID
    previewMaterialName_.clear();
    refreshMaterialPreview();
    return snapshot();
}
Json StudioSession::previewExternalAssets(const std::vector<std::filesystem::path>& files,
                                          const std::string& destination) {
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    pendingAssetTransfer_=StudioTransferPlan::external(ws_.project.dir,destination,files);
    return pendingAssetTransfer_->describe();
}
int StudioSession::selectedEntityIndex() const {
    if (selected_.empty()) return -1;
    try { return indexFor(selected_); }
    catch (const std::exception&) { return -1; }
}
Json StudioSession::dispatch(const Json& request) {
    Json reply = Json::object();
    reply.set("id", request["id"]);
    try {
        if (!request.isObject() || !request["id"].isNumber()) throw std::runtime_error("Invalid request envelope");
        const std::string method = request.getStr("method");
        const Json& params = request["params"];
        if (method == "studio.snapshot") {
            reply.set("result", snapshot());
        } else if(method=="studio.facialPreviewBind" ||
                  method=="studio.facialPreviewControls" ||
                  method=="studio.facialPreviewInfo") {
            if(ws_.project.dir.empty())
                throw std::runtime_error("Open a project first");
            if(method=="studio.facialPreviewBind") {
                if(!params["guid"].isString())
                    throw std::invalid_argument("Facial preview selection requires asset GUID string");
                const std::string guid=params.getStr("guid");
                // A clear operation detaches all staged controls without
                // touching the Scene GPU device or the document's saved rig.
                if(guid.empty()) {
                    facialPreview_.reset();
                    reply.set("result",facialPreview_.describe());
                } else {
                    StudioAssetCatalog catalog(ws_.project.dir);
                    catalog.rebuild();
                    reply.set("result",facialPreview_.bind(catalog,guid,params["rig"]));
                }
            } else if(method=="studio.facialPreviewControls") {
                const Json& revision=params["revision"];
                if(!revision.isNumber() || revision.toDouble()<0 ||
                   revision.toDouble()!=double(revision.integer()))
                    throw std::runtime_error("Invalid facial selection revision");
                reply.set("result",facialPreview_.controls(
                    uint64_t(revision.integer()),params["values"],
                    params["vectors"]));
            } else {
                reply.set("result",facialPreview_.describe());
            }
        } else if (method == "studio.facialRigList" ||
                   method == "studio.facialRigGet" ||
                   method == "studio.facialRigValidate" ||
                   method == "studio.facialRigSave") {
            // Project-bound authoring only. Native side is the sole owner of
            // filesystem access; browser controls pass a bounded simple name,
            // never arbitrary source/destination paths.
            if(method!="studio.facialRigValidate" && ws_.project.dir.empty())
                throw std::runtime_error("Open a project first");
            if(method=="studio.facialRigList") {
                reply.set("result",listStudioFacialRigs(ws_.project.dir));
            } else if(method=="studio.facialRigGet") {
                const std::string name=params.getStr("name");
                reply.set("result",readStudioFacialRig(ws_.project.dir,name));
            } else {
                const Json& doc=params["rig"];
                std::string reason;
                if(!validStudioFacialRigDraft(doc,&reason))
                    throw std::runtime_error(reason);
                if(method=="studio.facialRigValidate") {
                    Json validated=Json::object();
                    validated.set("valid",Json::fromBool(true));
                    validated.set("morphs",Json::number(int64_t(doc["morphs"].size())));
                    validated.set("ikUnits",Json::number(int64_t(doc["rig"].size())));
                    // This is schema validation only, not model channel or
                    // native MotionWeave compiler verification.
                    validated.set("runtimeVerified",Json::fromBool(false));
                    reply.set("result",std::move(validated));
                } else {
                    const std::string name=params.getStr("name");
                    reply.set("result",saveStudioFacialRig(ws_.project.dir,name,doc));
                }
            }
        } else if (method == "studio.shaderGraphs" || method == "studio.shaderGraphGet" ||
                   method == "studio.shaderGraphValidate" || method == "studio.shaderGraphSave" ||
                   method == "studio.shaderGraphCatalog") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.shaderGraphCatalog") {
                const Json raw=ws_.call("shader_graph_catalog",Json::object(),"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object(),catalog=Json::array();
                for(const Json& op:raw["nodes"].items()) {
                    if(catalog.size()>=256) break;
                    if(!op.isObject() || !validStudioShaderGraphName(op.getStr("id")))continue;
                    Json allowed=Json::object();
                    allowed.set("id",op["id"]);
                    allowed.set("inputs",op["inputs"]);
                    allowed.set("domains",op["domains"]);
                    catalog.push(std::move(allowed));
                }
                result.set("nodes",std::move(catalog));
                reply.set("result",std::move(result));
            } else if (method == "studio.shaderGraphs") {
                const Json raw=ws_.call("shader_graph_list",Json::object(),"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json names=Json::array();
                const Json& candidates=raw["graphs"];
                const size_t count=std::min(size_t(128),candidates.size());
                for(size_t i=0;i<count;++i) {
                    if(candidates[i].isString() &&
                       validStudioShaderGraphName(candidates[i].str()))
                        names.push(candidates[i]);
                }
                Json result=Json::object();
                result.set("graphs",std::move(names));
                reply.set("result",std::move(result));
            } else if (method == "studio.shaderGraphValidate" || method == "studio.shaderGraphSave") {
                const Json& draft=params["graph"];
                if (!validStudioShaderGraphDraft(draft))
                    throw std::runtime_error("Invalid or oversized Shader Graph draft");
                Json request=Json::object();
                request.set("graph",draft);
                if(method=="studio.shaderGraphSave") {
                    const std::string name=params.getStr("name");
                    if(!validStudioShaderGraphName(name))
                        throw std::runtime_error("Invalid Shader Graph filename");
                    request.set("name",str(name));
                }
                const Json raw=ws_.call(method=="studio.shaderGraphSave"?
                    "shader_graph_set":"shader_graph_validate",request,"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object();
                result.set("graph",raw["graph"]);
                result.set("nodes",raw["nodes"]);
                result.set("samples",raw["samples"]);
                // Shader source output is deliberately NOT passed to browser.
                reply.set("result",std::move(result));
            } else {
                const std::string name=params.getStr("name");
                if (!validStudioShaderGraphName(name))
                    throw std::runtime_error("Invalid Shader Graph name");
                Json request=Json::object();
                request.set("name",str(name));
                // Existing ShaderGraphOps validates the project-local name/path,
                // 256 KiB graph budget and the entire node graph via compiler.
                const Json raw=ws_.call("shader_graph_get",request,"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object();
                result.set("graph",raw["graph"]);
                result.set("nodes",raw["nodes"]);
                result.set("samples",raw["samples"]);
                // Do NOT serialize raw.glsl/msl/vertex* to JS.
                reply.set("result",std::move(result));
            }
        } else if (method == "studio.materialPreview") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const std::string name=params.getStr("name");
            if(!name.empty() && !validStudioMaterialName(name))
                throw std::runtime_error("Invalid material name");
            if(!name.empty() && !editor::loadMaterialLibrary(ws_)[name].isObject())
                throw std::runtime_error("Unknown material");
            previewMaterialName_=name;
            refreshMaterialPreview();
            Json result=Json::object();
            result.set("active",Json::fromBool(!name.empty()));
            result.set("name",str(name));
            reply.set("result",std::move(result));
        } else if (method == "studio.materials" || method == "studio.materialPatch" ||
                   method == "studio.materialCreate") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.materialCreate") {
                const std::string name=params.getStr("name");
                const std::string preset=params.getStr("preset");
                if (!validStudioMaterialName(name) || !validStudioMaterialPreset(preset))
                    throw std::runtime_error("Invalid material name or preset");
                const Json existing=editor::loadMaterialLibrary(ws_);
                if (existing.has(name)) throw std::runtime_error("A material with this name already exists");

                Json material=Json::object();
                Json color=Json::array();
                const double rgb[3] = {
                    preset=="metal" ? .78 : preset=="glass" ? .8 : .65,
                    preset=="metal" ? .8 : preset=="glass" ? .92 : .68,
                    preset=="metal" ? .85 : preset=="glass" ? 1.0 : .72
                };
                for(double v:rgb) color.push(Json::number(v,3));
                material.set("color",std::move(color));
                material.set("metallic",Json::number(preset=="metal" ? 1.0 : 0.0,3));
                material.set("roughness",Json::number(preset=="metal" ? .32 : preset=="glass" ? .08 : .85,3));
                material.set("alpha",str(preset=="glass" ? "blend" : "opaque"));
                if (preset=="glass") material.set("doubleSided",Json::fromBool(true));
                Json request=Json::object();
                request.set("name",str(name));
                request.set("material",std::move(material));
                const Json saved=ws_.call("material_set",request,"studio");
                if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
            } else if (method == "studio.materialPatch") {
                const std::string name=params.getStr("name");
                if (!validStudioMaterialName(name))
                    throw std::runtime_error("Invalid material name");
                const Json& patch=params["patch"];
                if (!patch.isObject() || patch.size()==0 || patch.size()>15)
                    throw std::runtime_error("Material patch requires 1..15 safe fields");
                const Json library=editor::loadMaterialLibrary(ws_);
                if (!library[name].isObject())
                    throw std::runtime_error("Only existing library materials can be edited");
                for(const auto& [key,value]:patch.members()) {
                    if (value.isNumber()) {
                        if (!validStudioMaterialNumber(key,value.toDouble()))
                            throw std::runtime_error("Unsupported material number: "+key);
                    } else if (value.isString()) {
                        if (!validStudioMaterialText(key,value.str()))
                            throw std::runtime_error("Unsupported material text: "+key);
                    } else if (value.type()==Json::Type::Bool) {
                        if (!validStudioMaterialBoolean(key))
                            throw std::runtime_error("Unsupported material boolean: "+key);
                    } else throw std::runtime_error("Unsupported material value type: "+key);
                }
                Json request=Json::object();
                request.set("name",str(name));
                request.set("material",patch);
                request.set("merge",Json::fromBool(true));
                // material_set validates keys/base and writes materials.json atomically.
                // Unlike MapDoc edits this is immediate disk save without Undo:
                // explicit Apply is required in the frontend.
                const Json result=ws_.call("material_set",request,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
            }
            if (method!="studio.materials" && !previewMaterialName_.empty()) {
                // Refresh preview when the selected disk material was modified,
                // including inherited/base changes.
                refreshMaterialPreview();
            }
            const Json library=editor::loadMaterialLibrary(ws_);
            Json result=Json::object();
            result.set("library",library);
            reply.set("result",std::move(result));
        } else if (method == "studio.lighting" || method == "studio.lightingSet" ||
                   method == "studio.lightAdd" || method == "studio.lightUpdate" ||
                   method == "studio.lightDelete") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.project.isFps()) {
                // FPS maps serialize MapData.fps, NOT MapData.extra. Apply to a copy,
                // then validate/commit the full candidate through fps_set.
                const Json currentDocument = ws_.map.toJson();
                if (method != "studio.lighting") {
                    const Json proposed = editFpsLighting(currentDocument,method,params);
                    if (!(proposed == currentDocument)) {
                        Json input=Json::object();
                        input.set("map",proposed);
                        const Json updated=ws_.call("fps_set",input,"studio");
                        if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
                    }
                }
                reply.set("result",fpsLightingView(ws_.map.toJson()));
                return reply;
            }
            if (method == "studio.lightingSet") {
                const std::string key = params.getStr("key");
                const Json& value = params["value"];
                if (!value.isNumber() || !validLightingScalar(key, value.toDouble()))
                    throw std::runtime_error("Invalid lighting parameter");
                Json input = Json::object();
                input.set(key, value);
                const Json updated = ws_.call("lighting_set", input, "studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            } else if (method == "studio.lightAdd") {
                Json input = Json::object();
                // New authored lights go to map center for flat maps; FPS origin otherwise.
                const double x = ws_.project.isFps() ? 0.0 : double(ws_.map.data.w) * 0.5;
                const double z = ws_.project.isFps() ? 0.0 : double(ws_.map.data.h) * 0.5;
                if (!validLightScalar("x",x) || !validLightScalar("z",z))
                    throw std::runtime_error("Map coordinates out of range");
                input.set("x", Json::number(x,3));
                input.set("z", Json::number(z,3));
                const Json updated = ws_.call("light_add", input, "studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            } else if (method == "studio.lightUpdate" || method == "studio.lightDelete") {
                const Json& rawIndex=params["index"];
                if (!rawIndex.isNumber() || rawIndex.toDouble() != double(rawIndex.integer()))
                    throw std::runtime_error("Light index must be an integer");
                const int64_t index = rawIndex.integer(-1);
                const Json& list = ws_.map.data.extra["lights"];
                if (index < 0 || !list.isArray() || uint64_t(index) >= list.size())
                    throw std::runtime_error("Unknown light index");
                Json input = Json::object();
                input.set("index", Json::number(index));
                if (method == "studio.lightUpdate") {
                    const std::string key = params.getStr("key");
                    const Json& value = params["value"];
                    if (value.isNumber()) {
                        if (!validLightScalar(key,value.toDouble()))
                            throw std::runtime_error("Invalid light numeric property");
                    } else if (value.isString()) {
                        if (!validLightText(key,value.str()))
                            throw std::runtime_error("Invalid light string property");
                    } else if (value.type() == Json::Type::Bool) {
                        if (!validLightBoolean(key))
                            throw std::runtime_error("Invalid light boolean property");
                    } else throw std::runtime_error("Invalid light property type");
                    input.set(key,value);
                }
                const Json updated = ws_.call(method == "studio.lightDelete" ? "light_delete" : "light_update",input,"studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            }
            const Json current = ws_.call("light_list",Json::object(),"studio");
            if (current.has("error")) throw std::runtime_error(current.getStr("error"));
            reply.set("result",current);
        } else if (method == "studio.assets") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            // Browser input cannot specify a filesystem directory. This scan
            // sees ONLY regular files beneath project/content (no symlinks).
            // Return content-relative paths, so the UI's root is "content".
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild(); // .meta is source of truth, index is disposable
            Json r = Json::object();
            r.set("root", str("content"));
            Json entries = Json::array(), folders = Json::array();
            for (const auto& asset : catalog.all()) {
                Json item = Json::object();
                item.set("path", str(asset.path));
                item.set("guid", str(asset.guid));
                item.set("importer",str(asset.importer));
                item.set("bytes", Json::number(asset.bytes));
                if(asset.directory)folders.push(std::move(item));
                else entries.push(std::move(item));
            }
            r.set("files",std::move(entries));
            r.set("folders",std::move(folders));
            reply.set("result", std::move(r));
        } else if (method == "studio.assetMetaPatch") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            const std::string key=params.getStr("key");
            if(!studioValidGuid(guid)||key.size()>40||!params.has("value"))
                throw std::runtime_error("Invalid asset import setting request");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto updated=catalog.patchSettings(guid,key,params["value"]);
            Json detail=Json::object();
            detail.set("guid",str(updated.guid));
            detail.set("path",str(updated.path));
            detail.set("importer",str(updated.importer));
            detail.set("directory",Json::fromBool(updated.directory));
            detail.set("bytes",Json::number(updated.bytes));
            detail.set("settings",updated.settings);
            reply.set("result",std::move(detail));
        } else if (method == "studio.assetFolderCreate") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string path=studioCreateContentFolder(ws_.project.dir,
                params.getStr("parent"),params.getStr("name"));
            Json r=Json::object();
            r.set("path",str(path));reply.set("result",std::move(r));
        } else if (method == "studio.assetMovePreview") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const Json& paths=params["paths"];
            if(!paths.isArray()||paths.size()<1||paths.size()>64)
                throw std::runtime_error("Select between 1 and 64 assets");
            std::vector<std::string> sources;
            for(const Json& path:paths.items()){
                if(!path.isString())throw std::runtime_error("Invalid source path");
                sources.push_back(path.str());
            }
            pendingAssetTransfer_=StudioTransferPlan::internal(ws_.project.dir,
                params.getStr("destination"),sources);
            reply.set("result",pendingAssetTransfer_->describe());
        } else if (method == "studio.assetTransferCommit") {
            if(!pendingAssetTransfer_)throw std::runtime_error("No active asset transfer preview");
            const auto token=params.getStr("token");
            const Json& raw=params["decisions"];
            if(!raw.isArray()||raw.size()>64)throw std::runtime_error("Invalid conflict decisions");
            std::vector<StudioTransferDecision> decisions;
            for(const Json& d:raw.items()){
                if(!d.isObject()||!d["id"].isNumber()||!d["action"].isString())
                    throw std::runtime_error("Invalid conflict decision");
                decisions.push_back({int(d["id"].integer()),d.getStr("action")});
            }
            const Json result=pendingAssetTransfer_->apply(token,decisions);
            pendingAssetTransfer_.reset();
            reply.set("result",result);
        } else if (method == "studio.assetByGuid") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            if(!studioValidGuid(guid))throw std::runtime_error("Invalid asset GUID");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto entry=catalog.byGuid(guid);
            if(!entry)throw std::runtime_error("Asset GUID not found; refresh Assets");
            Json item=Json::object();
            item.set("guid",str(entry->guid));
            item.set("path",str(entry->path));
            item.set("importer",str(entry->importer));
            item.set("directory",Json::fromBool(entry->directory));
            item.set("bytes",Json::number(entry->bytes));
            item.set("settings",entry->settings);
            reply.set("result",std::move(item));
        } else if (method == "studio.select") {
            const Json& id = params["id"];
            if (!id.isNull() && !id.isString()) throw std::runtime_error("Invalid selection");
            const std::string next = id.isNull() ? "" : id.str();
            if (!next.empty()) {
                bool found = false;
                for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map, ws_.project.isFps())) {
                    if (nodeId(node.ref) == next) { found = true; break; }
                }
                if (!found) throw std::runtime_error("Selection refers to an unknown document entity");
            }
            selected_ = next;
            reply.set("result", snapshot());
        } else if (method == "studio.save") {
            const Json r = ws_.call("map_save", Json::object(), "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            reply.set("result", snapshot());
        } else if (method == "studio.undo") {
            if (!ws_.map.canUndo()) throw std::runtime_error("Nothing to undo");
            ws_.map.undo();
            reply.set("result", snapshot());
        } else if (method == "studio.redo") {
            if (!ws_.map.canRedo()) throw std::runtime_error("Nothing to redo");
            ws_.map.redo();
            reply.set("result", snapshot());
        } else if (method == "studio.propertiesFor") {
            reply.set("result", propertiesFor(params.getStr("entityId")));
        } else if (method == "studio.setProperty") {
            const std::string entityId = params.getStr("entityId");
            // A pinned Inspector may edit its own explicitly addressed entity while
            // the global selection points elsewhere. Both document branches below
            // validate the target ID against the current project before writing.
            if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
            const std::string key = params.getStr("key");
            const Json& value = params["value"];
            if (entityId.compare(0, 7, "entity:") != 0) {
                // Non-map document sources use the shared editor transform adapter.
                // fps_set validates the COMPLETE edited document atomically before
                // committing it with the existing MapDoc undo snapshots.
                if (!ws_.project.isFps() ||
                    (key != "x" && key != "y" && key != "z") || !value.isNumber())
                    throw std::runtime_error("Unsupported document property");
                const double number = value.toDouble();
                if (!std::isfinite(number) || number < -1000000 || number > 1000000)
                    throw std::runtime_error("Invalid coordinate");

                bool found = false;
                editor::DocumentEntityRef ref;
                editor::DocumentTransform target{};
                for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map, true)) {
                    if (nodeId(node.ref) != entityId) continue;
                    if (node.ref.source == editor::DocumentEntityRef::Source::MapEntity ||
                        !node.hasPosition)
                        throw std::runtime_error("Document position is unavailable");
                    ref = node.ref;
                    target = node.position;
                    found = true;
                    break;
                }
                if (!found) throw std::runtime_error("Unknown document object");
                const double previous = key == "x" ? target.x : key == "y" ? target.y : target.z;
                if (previous == number) {
                    reply.set("result", snapshot());
                    return reply;
                }
                if (key == "x") target.x = number;
                else if (key == "y") target.y = number;
                else target.z = number;

                editor::MapDoc draft;
                draft.data.fps = ws_.map.toJson();
                if (!editor::setDocumentPosition(draft, ref, target))
                    throw std::runtime_error("Document transform validation failed");
                if (draft.toJson() == ws_.map.toJson()) {
                    reply.set("result", snapshot());
                    return reply; // serialized 3-decimal position unchanged
                }
                Json args = Json::object();
                args.set("map", draft.toJson());
                const Json op = ws_.call("fps_set", args, "studio");
                if (op.has("error")) throw std::runtime_error(op.getStr("error"));
                reply.set("result", snapshot());
                return reply;
            }
            const int index = indexFor(entityId);
            if (key == "type") {
                if (!value.isString() || value.str().empty()) throw std::runtime_error("Invalid type");
            } else if (key == "x" || key == "z" || key == "owner") {
                if (!value.isNumber()) throw std::runtime_error("Expected number");
                const double v = value.toDouble();
                if (!(v >= -1000000 && v <= 1000000)) throw std::runtime_error("Value out of range");
                if (key == "owner" && (v != double(value.integer()) || v < -1 || v > 10000))
                    throw std::runtime_error("Invalid owner");
            } else throw std::runtime_error("Property not editable");
            Json changes = Json::object(); changes.set(key, value);
            Json op = Json::object();
            op.set("index", num(index));
            op.set("set", std::move(changes));
            const Json r = ws_.call("entity_update", op, "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            reply.set("result", snapshot());
        } else throw std::runtime_error("Unknown Studio method");
    } catch (const std::exception& ex) {
        reply.set("error", str(ex.what()));
    }
    return reply;
}
}
