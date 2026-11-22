#include "Editor.h"
#include "EditorGenreUi.h"
#include "SceneRenderable.h"
#include "EditorEntityAdapter.h"
#include "EditorSceneNodes.h"
#include "EditorSelectionAdapter.h"
#include "EditorRuntimeEntityProvider.h"
#include "EditorComponentPanel.h"
#include "engine/actor/ActorModule.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
namespace aiverse::editor {
namespace {
std::string quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe expands these even within quotes; refuse paths/addresses containing them.
    if(s.find_first_of("\"%\r\n!")!=std::string::npos)throw std::runtime_error("unsupported shell characters in FPS launch path");
    return "\""+s+"\"";
#else
    std::string result="'";for(char c:s)result+=c=='\''?"'\\''":std::string(1,c);return result+"'";
#endif
}
render::float3 point(const Json& j){return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
}
// The FPS host stores characters, projectiles, loot and vehicles as ECS entities (ActorComponents.h).
// Expose a typed, read-only snapshot without pretending they are DocEntity or
// RTS World rows. This uses the same actor::Module::setup as the FPS client.
const Json& Editor::fpsRuntimeSnapshot() {
    const bool current = fpsRuntimeRevision_ == doc().revision &&
        fpsRuntimeRulesRevision_ == rulesRevision_ &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path;
    if (current) return fpsRuntimeCache_;
    // Do not repeatedly construct the entire simulation while the user drags
    // a Transform or paints. Refresh on release, as the RTS preview does.
    if (fpsRuntimeRevision_ != UINT64_MAX && ImGui::GetCurrentContext() &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle)) &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path)
        return fpsRuntimeCache_;
    fpsRuntimeRevision_ = doc().revision;
    fpsRuntimeRulesRevision_ = rulesRevision_;
    fpsRuntimeProjectDir_ = ws_.project.dir + "\n" + doc().path;
    fpsRuntimeCache_ = Json::object();
    Json actors = Json::array();
    if (!ws_.project.actorSessionPlay()) {
        fpsRuntimeCache_.set("actors", actors);
        return fpsRuntimeCache_;
    }
    try {
        const actor::Config config = actor::readConfig(ws_.project.rulesJson);
        Simulation preview({1, config.tickRate, 0}, std::make_unique<actor::Module>());
        preview.setup(ws_.project.rulesJson, doc().toJson());
        auto* module = dynamic_cast<actor::Module*>(&preview.module());
        if (!module) throw std::runtime_error("FPS module missing");
        // Characters and dynamic objects are ECS entities; the editor shows their real components
        // (CharacterId / CharacterBody / CharacterHealth / CharacterWeapons, Projectile / Loot / Vehicle).
        const World& world = preview.world();
        auto vec = [](Vec3 v) {
            Json out = Json::array();
            for (const Fixed value : {v.x, v.y, v.z}) out.push(Json::number(value.toDouble(), 3));
            return out;
        };
        const size_t characters = module->characterCount(preview);
        for (size_t slot = 1; slot <= characters; ++slot) {
            const Entity entity = module->characterEntity(preview, uint8_t(slot));
            if (!world.alive(entity)) continue;
            const auto* id = world.tryGetConst<actor::CharacterId>(entity);
            const auto* body = world.tryGetConst<actor::CharacterBody>(entity);
            const auto* health = world.tryGetConst<actor::CharacterHealth>(entity);
            const auto* weapons = world.tryGetConst<actor::CharacterWeapons>(entity);
            if (!id || !body || !health || !weapons) continue;
            const Json position = vec(body->pos), velocity = vec(body->velocity);
            Json data = Json::object();
            data.set("slot", Json::number(int64_t(id->slot)));
            data.set("team", Json::number(int64_t(id->team)));
            data.set("life", Json::number(int64_t(id->life)));
            data.set("ecsEntity", Json::number(int64_t(entity.packed())));
            data.set("position", position);
            data.set("velocity", velocity);
            data.set("yaw", Json::number(body->yaw.toDouble(), 3));
            data.set("pitch", Json::number(body->pitch.toDouble(), 3));
            data.set("health", Json::number(int64_t(health->health)));
            data.set("armor", Json::number(int64_t(health->armor)));
            data.set("ammo", Json::number(int64_t(weapons->ammo)));
            data.set("reserve", Json::number(int64_t(weapons->reserve)));
            Json components = Json::object();
            Json idFields = Json::object();
            idFields.set("slot", Json::number(int64_t(id->slot)));
            idFields.set("team", Json::number(int64_t(id->team)));
            idFields.set("life", Json::number(int64_t(id->life)));
            idFields.set("kills", Json::number(int64_t(id->kills)));
            components.set("CharacterId", idFields);
            Json bodyFields = Json::object();
            bodyFields.set("position", position);
            bodyFields.set("velocity", velocity);
            bodyFields.set("yaw", Json::number(body->yaw.toDouble(), 3));
            bodyFields.set("pitch", Json::number(body->pitch.toDouble(), 3));
            bodyFields.set("grounded", Json::fromBool(body->grounded));
            bodyFields.set("crouched", Json::fromBool(body->crouched));
            bodyFields.set("prone", Json::fromBool(body->prone));
            bodyFields.set("vehicle", Json::number(int64_t(body->vehicle)));
            components.set("CharacterBody", bodyFields);
            Json healthFields = Json::object();
            healthFields.set("health", Json::number(int64_t(health->health)));
            healthFields.set("armor", Json::number(int64_t(health->armor)));
            healthFields.set("downed", Json::fromBool(health->downed));
            healthFields.set("respawnAt", Json::number(int64_t(health->respawnAt)));
            components.set("CharacterHealth", healthFields);
            Json weaponFields = Json::object();
            weaponFields.set("weaponIndex", Json::number(int64_t(weapons->weaponIndex)));
            weaponFields.set("ammo", Json::number(int64_t(weapons->ammo)));
            weaponFields.set("reserve", Json::number(int64_t(weapons->reserve)));
            weaponFields.set("shots", Json::number(int64_t(weapons->shots)));
            weaponFields.set("hits", Json::number(int64_t(weapons->hits)));
            weaponFields.set("reloadUntil", Json::number(int64_t(weapons->reloadUntil)));
            weaponFields.set("nextFire", Json::number(int64_t(weapons->nextFire)));
            components.set("CharacterWeapons", weaponFields);
            data.set("components", components);
            actors.push(std::move(data));
        }
        // Dynamic objects keyed by kind << 32 | id (1 projectile, 2 loot, 3 vehicle), ascending id per kind.
        Json objects = Json::array();
        auto addObject = [&](uint8_t kind, uint32_t objectId, Entity entity, Vec3 position,
                             const char* component, Json fields, const std::string& title) {
            const uint64_t key = (uint64_t(kind) << 32) | objectId;
            const Json pos = vec(position);
            fields.set("position", pos);
            Json components = Json::object();
            components.set(component, std::move(fields));
            Json data = Json::object();
            data.set("key", Json::number(int64_t(key)));
            data.set("ecsEntity", Json::number(int64_t(entity.packed())));
            data.set("kind", Json::number(int64_t(kind)));
            data.set("title", Json::string(title));
            data.set("position", pos);
            data.set("components", components);
            objects.push(std::move(data));
        };
        for (const Entity entity : module->projectileEntities(preview)) {
            const auto* p = world.tryGetConst<actor::Projectile>(entity);
            if (!p) continue;
            Json fields = Json::object();
            fields.set("id", Json::number(int64_t(p->id)));
            fields.set("owner", Json::number(int64_t(p->owner)));
            fields.set("kind", Json::number(int64_t(p->kind)));
            fields.set("damage", Json::number(int64_t(p->damage)));
            fields.set("expires", Json::number(int64_t(p->expires)));
            fields.set("programEnabled", Json::fromBool(p->program.enabled));
            addObject(1, p->id, entity, p->pos, "Projectile", std::move(fields), "Projectile #" + std::to_string(p->id));
        }
        for (const Entity entity : module->lootEntities(preview)) {
            const auto* l = world.tryGetConst<actor::Loot>(entity);
            if (!l) continue;
            Json fields = Json::object();
            fields.set("id", Json::number(int64_t(l->id)));
            fields.set("type", Json::number(int64_t(l->type)));
            fields.set("count", Json::number(int64_t(l->count)));
            fields.set("ammo", Json::number(int64_t(l->ammo)));
            fields.set("reserve", Json::number(int64_t(l->reserve)));
            addObject(2, l->id, entity, l->pos, "Loot", std::move(fields), "Loot #" + std::to_string(l->id));
        }
        for (const Entity entity : module->vehicleEntities(preview)) {
            const auto* v = world.tryGetConst<actor::Vehicle>(entity);
            if (!v) continue;
            Json fields = Json::object();
            fields.set("id", Json::number(int64_t(v->id)));
            fields.set("health", Json::number(int64_t(v->health)));
            fields.set("driver", Json::number(int64_t(v->driver)));
            fields.set("yaw", Json::number(v->yaw.toDouble(), 3));
            fields.set("speed", Json::number(v->speed.toDouble(), 3));
            addObject(3, v->id, entity, v->pos, "Vehicle", std::move(fields), "Vehicle #" + std::to_string(v->id));
        }
        fpsRuntimeCache_.set("objects", objects);
    } catch (const std::exception& e) {
        fpsRuntimeCache_.set("error", Json::string(e.what()));
    }
    fpsRuntimeCache_.set("actors", actors);
    return fpsRuntimeCache_;
}

void Editor::startFpsPlay(bool listen,const std::string& address) {
    if(!ws_.project.actorSessionPlay()||ws_.building())return;
    try {
        auto cfg=actor::readConfig(ws_.project.rulesJson);Simulation check({1,cfg.tickRate,0},std::make_unique<actor::Module>());check.setup(ws_.project.rulesJson,doc().toJson());
        std::string error;
        if(doc().dirty&&!saveMap(doc().path))return;
        if(!ws_.project.saveRules(&error)||!ws_.project.saveManifest(&error))throw std::runtime_error(error);
        namespace fs=std::filesystem;
        // the one game client (apps/client): it runs the project's host from game.json "module"
        std::string name="AiverseGame";
#ifdef _WIN32
        name+=".exe";
#endif
        std::vector<fs::path> roots{fs::path(opt_.editorExe).parent_path(),fs::path(ws_.buildTool).parent_path(),fs::path(ws_.engineDir)/"bin"};
        auto parent=fs::path(opt_.editorExe).parent_path();for(int i=0;i<4;++i){roots.push_back(parent);parent=parent.parent_path();}
        fs::path exe;std::error_code ec;
        for(const auto& root:roots)for(const auto& rel:{fs::path(name),fs::path("Release")/name,fs::path("AiverseGame.app/Contents/MacOS")/name})if(exe.empty()&&fs::is_regular_file(root/rel,ec))exe=root/rel;
        if(exe.empty()){status=engineText(ws_,"fps.editor.missing");ws_.log("fps",status,true);return;}
        std::string cmd="cd "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#ifdef _WIN32
        cmd="cd /d "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#endif
        if(listen)cmd+=" --listen";else if(!address.empty())cmd+=" --connect "+quote(address);
        ws_.startCommand(cmd,cmd,"fps-play");status=engineText(ws_,"fps.editor.running");
    }catch(const std::exception& e){status=engineText(ws_,"fps.editor.invalid");ws_.log("fps",e.what(),true);}
}
// FPS contributes scene geometry to the shared editor renderer. The scene
// lifecycle, post-processing, depth buffer and UI overlays are owned by
// Editor::renderScene, not by a genre-specific render pass.
void Editor::enqueueFpsSceneGeometry(const render::Camera& camera, bool editOverlays) {
    using namespace render;
    if (!fpsPreviewMesh_.vb.valid()) {
        MeshBuilder mesh;
        mesh.color(rgba(255,255,255)).box({0,0.5f,0},{1,1,1});
        fpsPreviewMesh_ = mesh.upload(*dev_);
    }
    const uint64_t selectedKey = selection.kind == Selection::ActorObject ? selection.ref : 0;
    fpsSceneObjects_.clear();
    auto& instances = fpsSceneObjects_;
    const auto& fps = doc().data.fps;
    for (const auto& node : enumerateDocumentSceneNodes(doc(), true)) {
        if (!node.hasPosition) continue;
        const Json* source = fpsDocumentEntity(fps, node.ref);
        if (!source) continue;
        SceneRenderable object;
        object.entity = node.ref.fpsKey();
        object.mesh = &fpsPreviewMesh_;
        object.pickable = true;
        if (node.ref.source == DocumentEntityRef::Source::ActorCollider) {
            if ((*source)["yaw"].isNumber()) {
                const Json& half = (*source)["half"];
                if (!half.isArray() || half.size() != 3 ||
                    !half[0].isNumber() || !half[1].isNumber() ||
                    !half[2].isNumber()) continue;
                const float3 centre{float(node.position.x),
                                    float(node.position.y),
                                    float(node.position.z)};
                const float3 extents = point(half);
                if (!std::isfinite(extents.x) || !std::isfinite(extents.y) ||
                    !std::isfinite(extents.z) || extents.x <= 0 ||
                    extents.y <= 0 || extents.z <= 0) continue;
                const float angle = float((*source)["yaw"].toDouble() *
                                          6.283185307179586);
                const float cs = std::abs(std::cos(angle));
                const float sn = std::abs(std::sin(angle));
                const float3 bounds{
                    cs * extents.x + sn * extents.z,
                    extents.y,
                    sn * extents.x + cs * extents.z
                };
                object.world = mat4::translate({centre.x, centre.y - extents.y,
                                               centre.z}) *
                               mat4::rotateY(angle) *
                               mat4::scale(extents * 2.0f);
                object.tint = {0.35f, 0.42f, 0.44f, 1};
                object.boundsMin = centre - bounds;
                object.boundsMax = centre + bounds;
            } else {
                const Json& lower = (*source)["min"];
                const Json& upper = (*source)["max"];
                if (!lower.isArray() || !upper.isArray() ||
                    lower.size() < 3 || upper.size() < 3) continue;
                const float3 lo = point(lower), hi = point(upper);
                const float3 size = hi - lo;
                if (!std::isfinite(lo.x) || !std::isfinite(lo.y) ||
                    !std::isfinite(lo.z) || !std::isfinite(hi.x) ||
                    !std::isfinite(hi.y) || !std::isfinite(hi.z) ||
                    size.x <= 0 || size.y <= 0 || size.z <= 0) continue;
                const float3 base{float(node.position.x), lo.y,
                                  float(node.position.z)};
                if (length(base - camera.target) > 1000 && hi.y > 0) continue;
                object.world = mat4::translate(base) * mat4::scale(size);
                object.tint = {0.35f, 0.42f, 0.44f, 1};
                object.boundsMin = lo;
                object.boundsMax = hi;
            }
        } else if (node.ref.source == DocumentEntityRef::Source::ActorSpawn) {
            const float3 pos{float(node.position.x), float(node.position.y),
                             float(node.position.z)};
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
                !std::isfinite(pos.z)) continue;
            object.world = mat4::translate(pos) * mat4::scale({0.6f, 1.8f, 0.6f});
            object.tint = {0.1f, 0.65f, 0.85f, 1};
            object.boundsMin = {pos.x - 0.3f, pos.y, pos.z - 0.3f};
            object.boundsMax = {pos.x + 0.3f, pos.y + 1.8f, pos.z + 0.3f};
        } else {
            continue;
        }
        instances.push_back(object);
    }
    // Live objects are now real ECS entity components (the FPS gameplay state
    // remains the compatibility authority). All runtime genres enter through
    // the same editor runtime-entity provider.
    for (const auto& runtime : enumerateEditorRuntimeEntities(*this)) {
        if (!runtime.hasPosition) continue;
        const bool actorSource = runtime.selection.kind == Selection::ActorActor;
        const bool dynamicSource = runtime.selection.kind == Selection::ActorRuntimeObject;
        if (!actorSource && !dynamicSource) continue;
        const float3 point = runtime.position;
        SceneRenderable object;
        if (actorSource)
            object.runtimeActor = runtime.selection.index;
        else
            object.runtimeObject = runtime.selection.ref;
        float3 size = actorSource ? float3{0.5f, 1.7f, 0.5f}
                                 : float3{0.35f, 0.35f, 0.35f};
        if (dynamicSource) {
            const uint8_t kind = uint8_t(runtime.selection.ref >> 32);
            if (kind == 3) size = {1.6f, 1.1f, 3.0f}; // vehicle marker
            if (kind == 1) size = {0.18f, 0.18f, 0.18f}; // projectile marker
        }
        object.world = mat4::translate(point) * mat4::scale(size);
        object.tint = actorSource ? float4{0.85f, 0.68f, 0.25f, 1}
                          : float4{0.75f, 0.60f, 0.85f, 1};
        object.mesh = &fpsPreviewMesh_;
        object.pickable = true;
        object.boundsMin = {point.x - size.x * 0.5f, point.y,
                            point.z - size.z * 0.5f};
        object.boundsMax = {point.x + size.x * 0.5f, point.y + size.y,
                            point.z + size.z * 0.5f};
        instances.push_back(object);
    }
    for (auto& object : instances) {
        Selection candidate;
        if (object.runtimeObject) {
            candidate.kind = Selection::ActorRuntimeObject;
            candidate.ref = object.runtimeObject;
        } else if (object.runtimeActor >= 0) {
            candidate.kind = Selection::ActorActor;
            candidate.index = object.runtimeActor;
        } else {
            candidate.kind = Selection::ActorObject;
            candidate.ref = object.entity;
        }
        object.selected = editOverlays && (selection == candidate ||
            std::find(selectionSet.begin(), selectionSet.end(), candidate) !=
                selectionSet.end());
        object.hovered = editOverlays && !object.selected && hover == candidate;
    }
    submitSceneRenderables(r3d_, instances);
    // Draw transform axes from the selected renderable, if visible.
    for (const auto& selected : instances) {
        // The gizmo belongs to the primary selected entity, not whichever
        // selected collider happens to come first in source order.
        if (!selected.selected || selected.runtimeActor >= 0 ||
            selected.entity != selectedKey) continue;
        DocumentEntityRef ref;
        DocumentTransform position;
        if (!DocumentEntityRef::fromFpsKey(selected.entity, ref) ||
            !readDocumentTransform(doc(), ref, position)) break;
        const float3 centre{float(position.x), float(position.y),
                            float(position.z)};
        const float extent = std::max(0.35f, camera.distance * 0.075f);
        const Json* source = fpsDocumentEntity(doc().data.fps, ref);
        const bool oriented = source && (*source)["yaw"].isNumber();
        if (oriented && transformTool == TransformTool::Rotate) {
            constexpr int segments = 64;
            constexpr float tau = 6.283185307179586f;
            for (int i = 0; i < segments; ++i) {
                const float a = tau * float(i) / float(segments);
                const float b = tau * float(i + 1) / float(segments);
                r3d_.line(centre + float3{std::cos(a) * extent, 0,
                                          std::sin(a) * extent},
                          centre + float3{std::cos(b) * extent, 0,
                                          std::sin(b) * extent},
                          rgba(255, 188, 80));
            }
        } else {
            r3d_.line(centre, centre + float3{extent, 0, 0}, rgba(235, 84, 84));
            r3d_.line(centre, centre + float3{0, extent, 0}, rgba(93, 215, 113));
            r3d_.line(centre, centre + float3{0, 0, extent}, rgba(80, 153, 245));
        }
        break;
    }
}

bool Editor::moveSelectedFpsSceneObject(float dx, float dy, float dz) {
    // FPS uses the same atomic/deduplicated multi-document translation as
    // RTS. The selected set may contain colliders and spawn points together.
    std::vector<DocumentEntityRef> refs;
    for (const auto& chosen : selected()) {
        DocumentEntityRef ref;
        if (!documentEntityFromSelection(chosen, ref) ||
            ref.source == DocumentEntityRef::Source::MapEntity) continue;
        refs.push_back(ref);
    }
    return translateDocumentEntities(doc(), refs, {dx, dy, dz});
}

bool Editor::isFpsSceneSourceSelected(bool spawn, size_t sourceIndex) const {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection candidate = selectionForDocumentEntity(ref);
    return selection == candidate ||
        std::find(selectionSet.begin(), selectionSet.end(), candidate) != selectionSet.end();
}

void Editor::selectFpsSceneSource(bool spawn, size_t sourceIndex) {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection chosen = selectionForDocumentEntity(ref);
    if (chosen.kind != Selection::None) select(chosen);
}

int Editor::pickFpsSceneObject(const Viewport& vp, const render::Camera& camera,
                               float sx, float sy) const {
    render::float3 origin, direction;
    if (!camera.screenRay(sx - vp.originX, sy - vp.originY,
                          vp.pointsW, vp.pointsH, origin, direction)) return -1;
    return pickSceneRenderable(fpsSceneObjects_, origin, direction);
}


}
