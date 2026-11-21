#include "apps/editor/AuthoringWater.h"
#include "StudioSession.h"
#include "engine/world/NereidFlowAuthoring.h"
#include "apps/editor/core/ProjectFile.h"
#include "modules/rts/RtsMap.h"
#include "apps/editor/EditorWorldEdit.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "apps/editor/EditorSceneNodes.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioLightingValidation.h"
#include "engine/render/RenderLightingSettings.h"
#include "apps/studio-native/StudioFpsLightingDocument.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "apps/studio-native/StudioShaderGraphValidation.h"
#include "apps/studio-native/StudioShaderGraphDraft.h"
#include "apps/studio-native/StudioFacialRigDocument.h"
#include <algorithm>
#include "apps/editor/core/MaterialOps.h"
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>
#include <tuple>

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
std::string nodeId(const editor::MapDoc& map,const editor::DocumentEntityRef& ref) { return studioDocumentId(map,ref); }
// Resolve native picker/CLI inputs before locating the project-local journal.
// Workspace supports directories, manifest/project files and sample template IDs.
std::string recoveredProjectDir(const editor::Workspace& ws,const std::string& input) {
    std::string error;
    std::string directory=editor::resolveProjectDir(input,&error);
    if (directory.empty() && !editor::isProjectFilePath(input)) {
        const auto sample=std::filesystem::path(ws.gamesDir())/input;
        if (std::filesystem::exists(sample/"game.json")) directory=sample.string();
    }
    if (directory.empty()) throw std::runtime_error(error.empty()?"Cannot resolve project":error);
    directory=std::filesystem::weakly_canonical(directory).string();
    StudioMoveJournal::recover(directory);
    return directory;
}

}
StudioSession::StudioSession(const std::string& engineRoot, const std::string& project) {
    ws_.engineDir = engineRoot;
    if (!project.empty()) {
        const auto directory=recoveredProjectDir(ws_,project);
        std::string error;
        if (!ws_.openGame(directory, &error)) throw std::runtime_error(error.empty() ? "Cannot open project" : error);
        // A project opened from the CLI follows the same content policy as
        // projects chosen through the native Open Project panel.
        (void)studioContentRoot(ws_.project.dir);
    }
}
int StudioSession::indexFor(const std::string& id) const {
    const int index=studioMapEntityIndex(id,ws_.map);
   if(index<0)throw std::runtime_error("Invalid or missing entity ID");
   return index;
}
Json StudioSession::propertiesFor(const std::string& entityId) const {
    if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
    Json fields = Json::array();
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map, ws_.project.isFps())) {
        if (nodeId(ws_.map,node.ref) != entityId) continue;
        if(node.ref.isLight()) {
            const auto& light=*editor::documentLight(ws_.map,node.ref);
            fields.push(prop("name","Name","text",str(light.getStr("name"))));
            auto type=prop("type","Light type","text",str(light.getStr("type","point")));
            type.set("options",Json::parse(R"([{"label":"Point","value":"point"},{"label":"Spot","value":"spot"}])"));fields.push(type);
            for(const auto& [key,label,def,lo,hi]:std::vector<std::tuple<const char*,const char*,double,double,double>>{
                {"x","Position X",0,-1000000,1000000},
                {"height",ws_.project.isFps()?"Position Y":"Height above terrain",1,-1000,10000},{"z","Position Z",0,-1000000,1000000},{"radius","Radius",4,.001,100000},{"intensity","Intensity",1.5,0,10000}}) {
                auto field=numericProp(key,label,Json::number(light[key].toDouble(def),3));
                field.set("min",Json::number(lo));field.set("max",Json::number(hi));fields.push(field);
            }
            fields.push(prop("nightOnly","Night only","boolean",Json::fromBool(light.getBool("nightOnly"))));
            fields.push(prop("electric","Electric","boolean",Json::fromBool(light.getBool("electric"))));
        } else if (node.ref.source == editor::DocumentEntityRef::Source::MapEntity) {
            const auto& e = ws_.map.data.entities.at(node.ref.index);
            fields.push(prop("type", "Type", "text", str(e.type)));
            fields.push(numericProp("x", "Position X", Json::number(double(e.x), 4)));
            fields.push(numericProp("z", "Position Z", Json::number(double(e.z), 4)));
            fields.push(numericProp("owner", "Owner", num(int64_t(e.owner)), true));
           if(editor::authoredEntity(e)) {
               const auto& transform=e.extra["components"]["Transform"];
               fields.push(numericProp("y","Position Y",transform.has("y")?transform["y"]:Json::number(0.0)));
               if(!e.extra["components"].has("NereidWater")) {
                   fields.push(numericProp("yaw","Rotation Y",transform.has("yaw")?transform["yaw"]:Json::number(0.0)));
                   auto scale=numericProp("scale","Uniform scale",transform.has("scale")?transform["scale"]:Json::number(1.0));scale.set("min",Json::number(.001));scale.set("max",Json::number(10000.0));fields.push(scale);
               }
               fields.push(prop("asset","Model","text",str(editor::authoredModelAsset(e)),false));
           }
        } else if (node.hasPosition) {
            fields.push(numericProp("x", "Position X", Json::number(node.position.x, 3)));
            fields.push(numericProp("y", "Position Y", Json::number(node.position.y, 3)));
            fields.push(numericProp("z", "Position Z", Json::number(node.position.z, 3)));
        }
        return fields;
    }
    throw std::runtime_error("Unknown document object");
}

namespace {
Json component(const char* id,const char* title,Json properties,const char* note="") {
    Json c=Json::object();
    c.set("id",str(id));c.set("title",str(title));c.set("properties",std::move(properties));
    if(*note)c.set("note",str(note));
    return c;
}
Json readOnly(Json field){field.set("editable",Json::fromBool(false));return field;}
// Read-only display of an arbitrary authored component value.
Json genericField(const std::string& key,const Json& value) {
    Json r=Json::object();
    r.set("key",str(key));r.set("label",str(key));r.set("editable",Json::fromBool(false));
    if(value.isNumber()){r.set("type",str("number"));r.set("value",value);}
    else if(value.isString()){r.set("type",str("text"));r.set("value",value);}
    else if(value.boolean(false)==value.boolean(true)){r.set("type",str("boolean"));r.set("value",value);}
    else {r.set("type",str("text"));r.set("value",str(value.dump(-1)));}
    return r;
}
// Map "facing" is clockwise degrees seen from above; Transform rotation Y is
// right-handed (counter-clockwise). Both normalized to [0,360).
double studioFacingToYaw(double facing){double y=std::fmod(360.0-std::fmod(facing,360.0),360.0);return y<0?y+360.0:y;}
bool studioModelAssetPath(const std::string& path) {
    if(path.rfind("content/",0)!=0 || path.size()>512 || path.find("..")!=std::string::npos || path.find('\\')!=std::string::npos)
        return false;
    std::string ext=std::filesystem::path(path).extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    return ext==".glb" || ext==".gltf" || ext==".obj";
}
}
Json StudioSession::inspectorFor(const std::string& entityId) const {
    if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map, ws_.project.isFps())) {
        if (nodeId(ws_.map,node.ref) != entityId) continue;
        Json result=Json::object(),components=Json::array();
        result.set("id",str(entityId));result.set("name",str(node.label));
        if(node.ref.isLight()) {
            const auto& light=*editor::documentLight(ws_.map,node.ref);
            result.set("kind",str("Light"));
            Json transform=Json::array(),lightFields=Json::array();
            for(const auto& field:propertiesFor(entityId).items()) {
                const auto key=field.getStr("key");
                if(key=="name")continue;
                if(key=="x"||key=="z"||key=="height")transform.push(field); else lightFields.push(field);
            }
            components.push(component("Transform","Transform",transform));
            components.push(component("Light","Light",lightFields));
            result.set("nameEditable",Json::fromBool(true));
            (void)light;
        } else if(node.ref.source==editor::DocumentEntityRef::Source::MapEntity) {
            const auto& e=ws_.map.data.entities.at(node.ref.index);
            const bool authored=editor::authoredEntity(e);
            result.set("kind",str(e.type));
            const auto& comps=e.extra["components"];
            Json transform=Json::array();
            // Full 3D Transform for every map entity. Legacy units show their ground
            // height and runtime facing; the first Y/rotation/scale edit writes an
            // explicit components.Transform (see setProperty).
            const double levelHeight=ws_.project.rules.levelHeight.toDouble();
            const double ground=e.at.isNull()?terrainPreviewHeight(ws_.map.data,e.x,e.z,float(levelHeight)):0.0;
            const auto rotation=authored?editor::authoredRotationDegrees(e):
                std::array<double,3>{0.0,studioFacingToYaw(e.extra["facing"].toDouble(0)),0.0};
            const auto scale=authored?editor::authoredScale3(e):std::array<double,3>{1.0,1.0,1.0};
            const bool water=comps.has("NereidWater");
            transform.push(numericProp("x","Position X",Json::number(double(e.x),4)));
            transform.push(numericProp("y","Position Y",authored?Json::number(comps["Transform"]["y"].toDouble(0),4):Json::number(ground,4)));
            transform.push(numericProp("z","Position Z",Json::number(double(e.z),4)));
            const char* rotKeys[3]={"rotationX","rotationY","rotationZ"};
            const char* rotLabels[3]={"Rotation X","Rotation Y","Rotation Z"};
            const char* scaleKeys[3]={"scaleX","scaleY","scaleZ"};
            const char* scaleLabels[3]={"Scale X","Scale Y","Scale Z"};
            for(int i=0;i<3;++i){
                auto r=numericProp(rotKeys[i],rotLabels[i],Json::number(rotation[size_t(i)],3));
                r.set("min",Json::number(int64_t(-360000)));r.set("max",Json::number(int64_t(360000)));r.set("step",Json::number(int64_t(1)));
                if(water)r=readOnly(r);
                transform.push(r);
            }
            for(int i=0;i<3;++i){
                auto sc=numericProp(scaleKeys[i],scaleLabels[i],Json::number(scale[size_t(i)],4));
                sc.set("min",Json::number(.001));sc.set("max",Json::number(10000.0));
                if(water)sc=readOnly(sc);
                transform.push(sc);
            }
            components.push(component("Transform","Transform",transform));
            // Mesh Renderer: authored StaticModel (editable) or the type's view (read-only).
            const Json& view=ws_.project.rulesJson["types"][e.type]["view"];
            Json mesh=Json::array();
            const Json library=editor::loadMaterialLibrary(ws_);
            Json options=Json::array();
            {Json none=Json::object();none.set("label",str("(model default)"));none.set("value",str(""));options.push(none);}
            for(const auto& [name,value]:library.members()){(void)value;Json o=Json::object();o.set("label",str(name));o.set("value",str(name));options.push(o);}
            if(comps.has("StaticModel") || authored) {
                const std::string asset=editor::authoredModelAsset(e);
                Json field=prop("asset","Mesh","asset",str(asset),authored);
                Json kinds=Json::array();kinds.push(str("model"));field.set("assetKinds",kinds);
                mesh.push(field);
                const Json& overrides=comps["StaticModel"]["materials"];
                std::vector<std::string> slots;
                if(materialSlots_ && !asset.empty())slots=materialSlots_(asset);
                if(slots.empty())slots.push_back("*");
                for(const auto& [name,value]:overrides.members())
                    if(std::find(slots.begin(),slots.end(),name)==slots.end())slots.push_back(name);
                for(const auto& slot:slots) {
                    const std::string key="material:"+slot;
                    const std::string label=slot=="*"?"Material (all slots)":"Material · "+slot;
                    Json f=prop(key.c_str(),label.c_str(),"text",str(overrides[slot].isString()?overrides[slot].str():""),authored);
                    f.set("options",options);mesh.push(f);
                }
                components.push(component("MeshRenderer","Mesh Renderer",mesh,
                    asset.empty()?"No mesh assigned: the Scene shows a proxy box.":""));
            } else {
                const Json& model=view["model"];
                std::string path=model.isString()?model.str():model.isArray()&&model.size()?
                    (model[size_t(0)].isString()?model[size_t(0)].str():model[size_t(0)].getStr("file")):"";
                mesh.push(readOnly(prop("asset","Mesh","text",str(path.empty()?"(none)":path))));
                for(const auto& [slot,ref]:view["materials"].members())
                    mesh.push(readOnly(prop(("material:"+slot).c_str(),(slot=="*"?std::string("Material (all slots)"):"Material · "+slot).c_str(),
                        "text",str(ref.isString()?ref.str():ref.dump(-1)))));
                components.push(component("MeshRenderer","Mesh Renderer",mesh,
                    ("Defined by entity type '"+e.type+"' in rules.json.").c_str()));
            }
            Json unit=Json::array();
            unit.push(readOnly(prop("type","Type","text",str(e.type))));
            unit.push(numericProp("owner","Owner",num(int64_t(e.owner)),true));
            components.push(component("Unit","Unit",unit));
            for(const auto& [name,value]:comps.members()) {
                if(name=="Transform"||name=="StaticModel")continue;
                Json fields=Json::array();
                if(value.isObject())for(const auto& [key,item]:value.members())fields.push(genericField(key,item));
                else fields.push(genericField("value",value));
                components.push(component(name.c_str(),name.c_str(),fields,"Read-only in Studio."));
            }
        } else {
            result.set("kind",str(node.ref.source==editor::DocumentEntityRef::Source::FpsCollider?"Collider":"Spawn"));
            Json transform=Json::array();
            if(node.hasPosition) {
                transform.push(numericProp("x","Position X",Json::number(node.position.x,3)));
                transform.push(numericProp("y","Position Y",Json::number(node.position.y,3)));
                transform.push(numericProp("z","Position Z",Json::number(node.position.z,3)));
            }
            components.push(component("Transform","Transform",transform));
            for(const auto& descriptor:editor::documentComponents(ws_.map,node.ref)) {
                if(descriptor.kind==editor::DocumentComponentKind::Transform)continue;
                Json data,fields=Json::array();
                if(editor::readDocumentComponent(ws_.map,node.ref,descriptor.kind,data))
                    for(const auto& [key,item]:data.members())fields.push(genericField(key,item));
                components.push(component(descriptor.id,descriptor.title,fields,"Read-only in Studio."));
            }
        }
        result.set("components",components);
        return result;
    }
    throw std::runtime_error("Unknown document object");
}
Json StudioSession::setMeshRenderer(int index,const std::string& key,const Json& value) {
    if(index<0)throw std::runtime_error("Unknown document object");
    const auto& source=ws_.map.data.entities[size_t(index)];
    if(!editor::authoredEntity(source))
        throw std::runtime_error("This entity's mesh is defined by its type in rules.json");
    if(!value.isString())throw std::runtime_error("Mesh Renderer values are text");
    auto candidate=source;
    Json components=candidate.extra["components"];
    Json model=components["StaticModel"].isObject()?components["StaticModel"]:Json::object();
    if(key=="asset") {
        std::string path=value.str();
        if(!path.empty() && path.rfind("content/",0)!=0) {
            // The shared asset picker returns stable GUIDs; documents keep content paths.
            if(!studioValidGuid(path))throw std::runtime_error("Mesh must be a content/ path or asset GUID");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto entry=catalog.byGuid(path);
            if(!entry || entry->directory)throw std::runtime_error("Asset GUID not found; refresh Assets");
            path="content/"+entry->path;
        }
        if(!path.empty()) {
            if(!studioModelAssetPath(path))throw std::runtime_error("Mesh must be a .glb, .gltf or .obj file in content/");
            if(!std::filesystem::is_regular_file(std::filesystem::path(ws_.project.dir)/path))
                throw std::runtime_error("Mesh file does not exist: "+path);
            model.set("asset",str(path));
        } else model.erase("asset");
    } else if(key.rfind("material:",0)==0 && key.size()>9 && key.size()<=200) {
        const std::string slot=key.substr(9);
        Json materials=model["materials"].isObject()?model["materials"]:Json::object();
        if(value.str().empty())materials.erase(slot);
        else {
            if(!editor::loadMaterialLibrary(ws_).has(value.str()))
                throw std::runtime_error("Unknown material: "+value.str());
            materials.set(slot,value);
        }
        if(materials.size())model.set("materials",materials); else model.erase("materials");
    } else throw std::runtime_error("Unknown Mesh Renderer property");
    if(model.size())components.set("StaticModel",model); else components.erase("StaticModel");
    candidate.extra.set("components",components);
    if(candidate.extra!=source.extra) {
        ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
    }
    return snapshot();
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
        row.set("id", str(nodeId(ws_.map,node.ref)));
        row.set("name", str(node.label));
        // UI receives only generic display kind; document source names remain native.
        row.set("kind", str(node.ref.source == editor::DocumentEntityRef::Source::MapEntity
            ? (node.ref.index < ws_.map.data.entities.size() ? ws_.map.data.entities[node.ref.index].type : "Entity")
            : node.ref.isLight()?"Light":node.ref.source == editor::DocumentEntityRef::Source::FpsCollider ? "Collider" : "Spawn"));
        row.set("visible", Json::fromBool(true));
        rows.push(std::move(row));
    }
    scene.set("entities", std::move(rows));
    result.set("scene", std::move(scene));
    Json fields = Json::array(), componentViews=Json::array(), entityFields=Json::array();
    bool valid = false;
    if (!selected_.empty()) {
        for (const auto& node : nodes) {
            if (nodeId(ws_.map,node.ref) != selected_) continue;
            valid = true;
            fields = propertiesFor(selected_);
            if(node.ref.isLight()) {
                // Light entities expose Entity/Transform/Light components over the
                // same canonical document object; no separate light state exists.
                for(const auto& descriptor:editor::documentComponents(ws_.map,node.ref)) {
                    Json component=Json::object(),properties=Json::array();
                    component.set("id",str(descriptor.id));component.set("title",str(descriptor.title));
                    for(const auto& field:fields.items()) {
                        const auto key=field.getStr("key");
                        if(key=="name")continue;
                        const bool transform=key=="x" || key=="z" || key=="height";
                        if(transform==(descriptor.kind==editor::DocumentComponentKind::Transform))properties.push(field);
                    }
                    component.set("properties",properties);componentViews.push(component);
                }
                entityFields.push(fields[0]);fields=Json::array();
            }
            break;
        }
    }
    result.set("selectedId", valid ? str(selected_) : Json());
    result.set("properties", std::move(fields));
    result.set("components",std::move(componentViews));
    result.set("entityProperties",std::move(entityFields));
    if(valid) { try { result.set("inspector",inspectorFor(selected_)); } catch(const std::exception&) {} }
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
Json StudioSession::saveMapAs(const std::string& path) {
    if (ws_.project.dir.empty() || path.empty()) throw std::runtime_error("Open a project and choose a map file");
    namespace fs=std::filesystem;
    const auto root=fs::weakly_canonical(ws_.project.dir);
    const auto target=fs::weakly_canonical(path);
    const auto relative=target.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin()!="maps" || target.extension()!=".json")
        throw std::runtime_error("Choose a .json map inside the current project maps folder");
    Json args=Json::object();args.set("file",str(target.string()));args.set("addToGame",Json::fromBool(true));
    const auto result=ws_.call("map_save",args,"studio");
    if (result.has("error")) throw std::runtime_error(result.getStr("error"));
    std::string error;
    if (ws_.project.rulesDirty && !ws_.project.saveRules(&error)) throw std::runtime_error(error);
    if (ws_.project.manifestDirty && !ws_.project.saveManifest(&error)) throw std::runtime_error(error);
    return snapshot();
}
Json StudioSession::openProject(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Project path is empty");
    const auto directory=recoveredProjectDir(ws_,path);
    std::string error;
    if (!ws_.openGame(directory, &error))
        throw std::runtime_error(error.empty() ? "Unable to open project" : error);
    (void)studioContentRoot(ws_.project.dir);
    pendingAssetTransfer_.reset();
    selected_.clear();
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
        Json params = request["params"];
        if((method=="studio.lightUpdate" || method=="studio.lightDelete") && params.has("entityId")) {
            if(!params["entityId"].isString())throw std::runtime_error("Invalid light entity ID");
            bool found=false;
            for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map,ws_.project.isFps()))
                if(node.ref.isLight() && nodeId(ws_.map,node.ref)==params.getStr("entityId")) {
                    params.set("index",num(int64_t(node.ref.index)));found=true;break;
                }
            if(!found)throw std::runtime_error("Light entity no longer exists");
        }
        if (method == "studio.snapshot") {
            reply.set("result", snapshot());
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
                if (!patch.isObject() || patch.size()==0 || patch.size()>24)
                    throw std::runtime_error("Material patch requires 1..24 safe fields");
                const Json library=editor::loadMaterialLibrary(ws_);
                if (!library[name].isObject())
                    throw std::runtime_error("Only existing library materials can be edited");
                Json validatedPatch = Json::object();
                // Runtime materials.json stores project-relative texture paths.
                // The HTML5 picker sends immutable GUIDs; resolve/validate them
                // at the native trust boundary, never accept an arbitrary path.
                for(const auto& [key,value]:patch.members()) {
                    if (key=="sssColor") {
                        if(!value.isString())throw std::runtime_error("SSS color must be linear RGB");
                        const auto rgb=parseStudioSssColor(value.str());
                        if(!rgb)throw std::runtime_error("SSS color requires three values from 0 to 1");
                        Json color=Json::array();
                        for(double channel:*rgb)color.push(Json::number(channel));
                        validatedPatch.set(key,std::move(color));
                        continue;
                    }
                    if (studioMaterialTextureKey(key)) {
                        if (!value.isString())
                            throw std::runtime_error("Texture reference must be an asset GUID");
                        if (value.str().empty()) {
                            validatedPatch.set(key, str(""));
                            continue;
                        }
                        if (!studioValidGuid(value.str()))
                            throw std::runtime_error("Texture reference requires a valid asset GUID");
                        StudioAssetCatalog catalog(ws_.project.dir);
                        catalog.rebuild();
                        const auto asset = catalog.byGuid(value.str());
                        if (!asset || asset->directory || asset->importer != "texture")
                            throw std::runtime_error("Texture GUID not found or is not a texture");
                        validatedPatch.set(key, str("content/" + asset->path));
                        continue;
                    }
                    validatedPatch.set(key, value);
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
                request.set("material",std::move(validatedPatch));
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
        } else if(method=="studio.nereidFlow"||method=="studio.nereidFlowSet"){
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(ws_.project.isFps())throw std::runtime_error("NEREID authoring currently requires a flat map");
            if(method=="studio.nereidFlowSet"){
                if(!params["mode"].isString())throw std::runtime_error("Choose current mode");
                editor::editAuthoredWaterFlow(ws_.map,params);
            }
            reply.set("result",world::nereidFlowState(editor::authoredWaterDocument(ws_.map)));
        } else if(method=="studio.renderLighting" || method=="studio.renderLightingSet") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const Json current=ws_.map.toJson();
            Json settings=render::renderLightingSettings(current["renderLighting"]);
            if(method=="studio.renderLightingSet") {
                const Json& patch=params["patch"];
                if(!patch.isObject() || patch.size()==0 || patch.size()>10)
                    throw std::runtime_error("Lighting patch requires 1..10 settings");
                for(const auto& [key,value]:patch.members())settings.set(key,value);
                settings=render::renderLightingSettings(settings); // validate BEFORE beginEdit
                if(!(settings==current["renderLighting"])) {
                    if(ws_.project.isFps()) {
                        Json next=current;next.set("renderLighting",settings);
                        Json input=Json::object();input.set("map",next);
                        const auto updated=ws_.call("fps_set",input,"studio");
                        if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                    } else {
                        ws_.map.beginEdit();ws_.map.data.extra.set("renderLighting",settings);ws_.map.touched();
                    }
                }
            }
            reply.set("result",settings);
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
        } else if (method == "studio.assetReferences") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild();
            const auto entries=catalog.all();
            reply.set("result",studioReferenceGraphJson(studioCollectAssetReferences(
                editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,entries)));
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
            Json preview=pendingAssetTransfer_->describe();
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild();
            const auto dependencies=studioCollectAssetReferences(
                editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,catalog.all());
            preview.set("references",studioMoveReferenceImpact(preview,dependencies));
            reply.set("result",std::move(preview));
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
            // Server-side safeguard: a browser can change decision values
            // after preview. Never move a rules model reference that the
            // reference graph cannot rewrite safely across included rules files.
            Json currentPlan=pendingAssetTransfer_->describe();
            if (currentPlan.getStr("source")=="internal") {
                StudioAssetCatalog dependencyCatalog(ws_.project.dir);
                dependencyCatalog.rebuild();
                const auto dependencies=studioCollectAssetReferences(
                    editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,
                    dependencyCatalog.all());
                const Json impacted=studioMoveReferenceImpact(currentPlan,dependencies);
                for (const auto& reference:impacted.items()) {
                    if (reference.getBool("writable")) continue;
                    const int id=int(reference["id"].integer());
                    std::string action;
                    for (const auto& item:currentPlan["items"].items())
                        if (int(item["id"].integer())==id)
                            action=item.getStr("conflict").empty()?"import":"skip";
                    for (const auto& decision:decisions)
                        if (decision.id==id) action=decision.action;
                    if (action!="skip")
                        throw std::runtime_error("Asset move blocked: rules model reference "+
                            reference.getStr("owner")+" / "+reference.getStr("field")+
                            " cannot yet be rewritten safely. Skip the item or edit its rules origin first.");
                }
            }
            // Preload material document before touching assets, so malformed
            // JSON cannot trigger a partially completed internal move.
            const bool internal=currentPlan.getStr("source")=="internal";
            Json library=internal ? editor::loadMaterialLibrary(ws_) : Json::object();
            std::map<std::string,std::string> oldPathByGuid;
            if (internal) {
                StudioAssetCatalog beforeCatalog(ws_.project.dir);
                beforeCatalog.rebuild();
                for (const auto& asset:beforeCatalog.all())
                    if (!asset.directory) oldPathByGuid.emplace(asset.guid,asset.path);
            }
            std::size_t repaired=0;
            // This hook executes while internal file/.meta moves are still
            // reversible. If the material write throws, apply() reverses
            // every completed rename before returning an error.
            const auto repair=[&]() {
                StudioAssetCatalog afterCatalog(ws_.project.dir);
                afterCatalog.rebuild();
                std::map<std::string,std::string> relocated;
                for (const auto& asset:afterCatalog.all()) {
                    if (asset.directory) continue;
                    const auto found=oldPathByGuid.find(asset.guid);
                    if (found!=oldPathByGuid.end() && found->second!=asset.path)
                        relocated.emplace("content/"+found->second,"content/"+asset.path);
                }
                const std::size_t updates=studioRemapMaterialReferences(library,relocated);
                if (updates) editor::saveMaterialLibrary(ws_,library,
                    "Studio Assets: repair material paths after move");
                repaired=updates;
            };
            const std::function<void()> commitHook =
                internal ? std::function<void()>(repair) : std::function<void()>{};
            Json result=pendingAssetTransfer_->apply(token,decisions,commitHook);
            pendingAssetTransfer_.reset();
            if (internal) result.set("repairedMaterialReferences",num(int64_t(repaired)));
            reply.set("result",std::move(result));
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
                    if (nodeId(ws_.map,node.ref) == next) { found = true; break; }
                }
                if (!found) throw std::runtime_error("Selection refers to an unknown document entity");
            }
            selected_ = next;
            reply.set("result", snapshot());
        } else if (method == "studio.project") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            Json state=Json::object(),maps=Json::array();
            for (const auto& item:ws_.project.manifest["maps"].items()) {
                Json row=Json::object();row.set("file",str(item.getStr("file")));
                row.set("title",str(item.getStr("title",item.getStr("file"))));maps.push(std::move(row));
            }
            state.set("maps",std::move(maps));state.set("currentMap",str(ws_.map.path.empty()?"":ws_.relative(ws_.map.path)));
            state.set("canCreateMap",Json::fromBool(!ws_.project.isFps()));
            state.set("maxMapSize",num(game::kMaxMapSize));reply.set("result",std::move(state));
        } else if (method == "studio.openMap" || method == "studio.newMap") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.openMap") {
                const auto file=params.getStr("file");bool listed=false;
                for (const auto& item:ws_.project.manifest["maps"].items()) listed|=item.getStr("file")==file;
                if (file.empty() || !listed) throw std::runtime_error("Choose a map from the current project");
                namespace fs=std::filesystem;
                const auto root=fs::weakly_canonical(ws_.project.dir);
                const auto path=fs::weakly_canonical(root/file);
                const auto relative=path.lexically_relative(root);
                if (relative.empty() || relative.is_absolute() || *relative.begin()=="..")
                    throw std::runtime_error("Map path escapes the project");
                editor::MapDoc draft;std::string error;
                if (!draft.load(path.string(),&error)) throw std::runtime_error(error);
                if (ws_.project.isFps()!=draft.data.fps.isObject()) throw std::runtime_error("Map format does not match the project");
                draft.revision=ws_.map.revision+1;
                ws_.map=std::move(draft);
                ws_.note({editor::Change::Map,0,0,-1,-1,-1,"Opened map"});
            } else {
                if (ws_.project.isFps()) throw std::runtime_error("FPS map creation requires an FPS document template");
                for (const char* key:{"width","height"}) {
                    const double value=params[key].toDouble();
                    if (!params[key].isNumber() || !std::isfinite(value) || value!=std::floor(value) || value<8 || value>game::kMaxMapSize)
                        throw std::runtime_error("Invalid map dimensions");
                }
                const auto name=params.getStr("name");
                if (name.empty() || name.size()>256) throw std::runtime_error("Map name is required (maximum 256 bytes)");
                Json args=Json::object();for (const char* key:{"width","height","name"})args.set(key,params[key]);
                const auto result=ws_.call("map_new",args,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                ws_.map.touched();
            }
            selected_.clear();previewMaterialName_.clear();refreshMaterialPreview();
            reply.set("result",snapshot());
        } else if (method == "studio.entityCatalog") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            Json catalog=Json::object(), types=Json::array();
            for (const auto& type : ws_.project.rules.types) {
                Json row=Json::object(); row.set("id",str(type.id)); row.set("name",str(type.name));
                types.push(std::move(row));
            }
            catalog.set("types",std::move(types));
            catalog.set("mapEntities",Json::fromBool(!ws_.project.isFps()));
            reply.set("result",std::move(catalog));
        } else if (method == "studio.entityCreate" || method == "studio.entityDelete" ||
                   method == "studio.entityDuplicate") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.project.isFps()) throw std::runtime_error("This operation requires a map entity document");
            if (method == "studio.entityCreate") {
                const std::string type=params.getStr("type");
                if (ws_.project.rules.findType(type)<0) throw std::runtime_error("Unknown entity type");
                for (const char* key : {"x","z","owner"}) {
                    if (!params[key].isNumber()) throw std::runtime_error("Expected numeric entity field");
                    const double value=params[key].toDouble();
                    if (!std::isfinite(value) || value < -1000000 || value > 1000000)
                        throw std::runtime_error("Entity field out of range");
                }
                const double owner=params["owner"].toDouble();
                if (owner!=std::floor(owner) || owner < -1 || owner > 10000)
                    throw std::runtime_error("Invalid owner");
                Json args=Json::object();
                for (const char* key : {"type","x","z","owner"}) args.set(key,params[key]);
                const Json result=ws_.call("entity_place",args,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                selected_="entity:"+std::to_string(ws_.map.data.entities.size()-1);
            } else {
                const std::string id=params.getStr("entityId");
                if (selected_.empty() || id!=selected_) throw std::runtime_error("Selection changed while editing");
                const int index=indexFor(id);
                if (method == "studio.entityDelete") {
                    Json args=Json::object(); args.set("index",num(index));
                    const Json result=ws_.call("entity_delete",args,"studio");
                    if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                    selected_.clear();
                } else {
                    const auto copies=editor::worldedit::duplicate(ws_.map,{index});
                    if (copies.empty()) throw std::runtime_error("Unable to duplicate entity");
                    selected_=studioDocumentId(editor::DocumentEntityRef::mapEntity(size_t(copies.front())),ws_.map);
                }
            }
            reply.set("result",snapshot());
        } else if (method == "studio.entityMove") {
            // Hierarchy drag: reorder authored map entities (document order), undoable.
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.project.isFps()) throw std::runtime_error("This operation requires a map entity document");
            const int from=indexFor(params.getStr("entityId"));
            auto& entities=ws_.map.data.entities;
            int to=int(entities.size());
            if (params["targetId"].isString()) {
                to=indexFor(params.getStr("targetId"));
                if (params.getStr("placement")=="after") ++to;
            }
            if (to!=from && to!=from+1) {
                ws_.map.beginEdit();
                // Roads reference entities by index: remap them with the permutation.
                std::vector<int> order(entities.size());
                for (size_t i=0;i<order.size();++i) order[i]=int(i);
                order.erase(order.begin()+from);
                if (to>from) --to;
                order.insert(order.begin()+to,from);
                std::vector<int> newIndex(order.size());
                decltype(ws_.map.data.entities) reordered;reordered.reserve(entities.size());
                for (size_t k=0;k<order.size();++k){newIndex[size_t(order[k])]=int(k);reordered.push_back(std::move(entities[size_t(order[k])]));}
                entities=std::move(reordered);
                for (auto& [a,b]:ws_.map.data.roads){a=newIndex[size_t(a)];b=newIndex[size_t(b)];}
                ws_.map.touched();
                selected_=studioDocumentId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(to)));
            }
            reply.set("result",snapshot());
        } else if (method == "studio.save") {
            const Json r = ws_.call("map_save", Json::object(), "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            std::string error;
            if (ws_.project.rulesDirty && !ws_.project.saveRules(&error)) throw std::runtime_error(error);
            if (ws_.project.manifestDirty && !ws_.project.saveManifest(&error)) throw std::runtime_error(error);
            reply.set("result", snapshot());
        } else if (method == "studio.undo") {
            if (!ws_.map.canUndo()) throw std::runtime_error("Nothing to undo");
            ws_.map.undo();
            selected_.clear();
            reply.set("result", snapshot());
        } else if (method == "studio.redo") {
            if (!ws_.map.canRedo()) throw std::runtime_error("Nothing to redo");
            ws_.map.redo();
            selected_.clear();
            reply.set("result", snapshot());
        } else if (method == "studio.propertiesFor") {
            reply.set("result", propertiesFor(params.getStr("entityId")));
        } else if (method == "studio.inspect") {
            reply.set("result", inspectorFor(params.getStr("entityId")));
        } else if (method == "studio.setProperty") {
            const std::string entityId = params.getStr("entityId");
            // A pinned Inspector may edit its own explicitly addressed entity while
            // the global selection points elsewhere. Both document branches below
            // validate the target ID against the current project before writing.
            if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
            const std::string key = params.getStr("key");
            const Json& value = params["value"];
            if(params.getStr("componentId")=="MeshRenderer") {
                reply.set("result",setMeshRenderer(indexFor(entityId),key,value));
                return reply;
            }
            if(entityId.compare(0,6,"light:")==0) {
                bool found=false;size_t index=0;
                for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map,ws_.project.isFps()))
                    if(node.ref.isLight() && nodeId(ws_.map,node.ref)==entityId){index=node.ref.index;found=true;break;}
                if(!found)throw std::runtime_error("Light entity no longer exists");
                const std::string owner=key=="name"?"Entity":
                    (key=="x" || key=="z" || key=="height")?"Transform":"Light";
                if(params.has("componentId") && params.getStr("componentId")!=owner)
                    throw std::runtime_error("Property does not belong to this component");
                if(owner=="Entity") {
                    Json nested=Json::object(),input=Json::object();
                    nested.set("id",request["id"]);nested.set("method",str("studio.lightUpdate"));
                    input.set("index",num(int64_t(index)));input.set("key",str(key));input.set("value",value);nested.set("params",input);
                    const auto updated=dispatch(nested);
                    if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                } else {
                    editor::MapDoc draft;draft.data=ws_.map.data;
                    const auto ref=editor::DocumentEntityRef::light(index,ws_.project.isFps());
                    bool changed=false;
                    if(owner=="Transform") {
                        if(!value.isNumber() || !validLightScalar(key,value.toDouble()))
                            throw std::runtime_error("Invalid light transform");
                        editor::DocumentTransform position;
                        if(!editor::readDocumentTransform(draft,ref,position))throw std::runtime_error("Transform unavailable");
                        double& coordinate=key=="x"?position.x:key=="z"?position.z:position.y;
                        if(coordinate==value.toDouble()){reply.set("result",snapshot());return reply;}
                        coordinate=value.toDouble();changed=editor::setDocumentPosition(draft,ref,position);
                    } else {
                        const auto* current=editor::documentLight(draft,ref);
                        if(current && (*current)[key]==value){reply.set("result",snapshot());return reply;}
                        Json patch=Json::object();patch.set(key,value);
                        changed=editor::writeDocumentComponent(draft,ref,editor::DocumentComponentKind::Light,patch);
                    }
                    if(!changed)throw std::runtime_error("Invalid component property");
                    if(ws_.project.isFps()) {
                        Json args=Json::object();args.set("map",draft.toJson());
                        const auto updated=ws_.call("fps_set",args,"studio");
                        if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                    } else {ws_.map.beginEdit();ws_.map.data=std::move(draft.data);ws_.map.touched();}
                }
                reply.set("result",snapshot());return reply;
            }
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
                    if (nodeId(ws_.map,node.ref) != entityId) continue;
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
            if(editor::authoredTransformKey(key)) {
               const auto& source=ws_.map.data.entities[size_t(index)];
               auto candidate=source;
               const bool legacy=!editor::authoredEntity(source);
               if(legacy) {
                   // Promote: keep the unit exactly where/how it stands now.
                   Json t=Json::object();
                   t.set("y",Json::number(source.at.isNull()?double(terrainPreviewHeight(ws_.map.data,source.x,source.z,
                       float(ws_.project.rules.levelHeight.toDouble()))):0.0,4));
                   if(source.extra["facing"].isNumber())t.set("yaw",Json::number(studioFacingToYaw(source.extra["facing"].toDouble()),3));
                   auto components=candidate.extra["components"].isObject()?candidate.extra["components"]:Json::object();
                   components.set("Transform",t);candidate.extra.set("components",components);
               }
               candidate=editor::editAuthoredPlacement(candidate,key,value);
               // The RTS runtime reads "facing" for the initial rotation: keep it in sync.
               if((key=="rotationY"||key=="yaw") && (legacy || source.extra.has("facing")))
                   candidate.extra.set("facing",Json::number(studioFacingToYaw(editor::authoredRotationDegrees(candidate)[1]),3));
               if(candidate.extra!=source.extra){ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();}
               reply.set("result",snapshot());return reply;
           }
           if (key == "type") {
                if (!value.isString() || value.str().empty()) throw std::runtime_error("Invalid type");
            } else if (key == "facing") {
                if (!value.isNumber() || !std::isfinite(value.toDouble()) ||
                    value.toDouble() < 0 || value.toDouble() > 360) throw std::runtime_error("Rotation must be 0..360 degrees");
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
