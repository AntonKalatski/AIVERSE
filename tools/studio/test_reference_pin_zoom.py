"""Studio HTML5 asset chooser, pin and zoom integration contract.
These are source-level guards; native compilation and UI interaction still need QA.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "apps/studio-web/src"
NATIVE = ROOT / "apps/studio-native/StudioSession.cpp"


class StudioReferencePinZoomTests(unittest.TestCase):
    def source(self, relative):
        return (WEB / relative).read_text(encoding="utf-8")

    def test_asset_picker_reuses_native_catalog_and_existing_drag_mime(self):
        picker = self.source("components/AssetReferencePicker.tsx")
        assets = self.source("components/AssetsPanel.tsx")
        self.assertIn("application/x-aiverse-content-path", picker)
        self.assertIn("application/x-aiverse-content-path", assets)
        self.assertIn("invoke('studio.assets'", picker)
        self.assertIn("onChange(asset.guid);", picker)
        self.assertIn("Search assets", picker)
        self.assertIn("event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)", picker)
        self.assertIn("result.files.find(item => item.path === path)", picker)
        self.assertIn("if (!asset.guid)", picker)
        self.assertNotIn("else if (path &&", picker)

    def test_reference_fields_are_schema_typed(self):
        schema = self.source("propertyLogic.ts")
        field = self.source("components/PropertyField.tsx")
        self.assertIn("'asset'", schema)
        self.assertIn("assetKinds?", schema)
        self.assertIn("property.type === 'asset'", field)
        self.assertIn("<AssetReferencePicker", field)

    def test_pinned_inspector_does_not_write_to_global_selection(self):
        app = self.source("App.tsx")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("inspectorPin?.entity.id ?? snapshot?.selectedId", app)
        self.assertIn("inspectorProperties", app)
        self.assertIn("setInspectorPin(null);", app)
        self.assertIn('if (entityId.empty()) throw std::runtime_error("Missing document entity ID");', native)
        self.assertNotIn('selected_ != entityId) throw std::runtime_error("Selection changed while editing")', native)
        self.assertIn("const int index = indexFor(entityId);", native)
        self.assertIn('method == "studio.propertiesFor"', native)
        self.assertIn("propertiesFor(params.getStr", native)
        self.assertIn("studio.propertiesFor", app)
        panel = self.source("panels/StudioPanelRenderer.tsx")
        self.assertIn("toggleInspectorPin", panel)
        self.assertIn("aria-pressed={inspectorPinned}", panel)

    def test_native_material_reference_slots_validate_guid_and_importer(self):
        native = NATIVE.read_text(encoding="utf-8")
        validation = (ROOT/"apps/studio-native/StudioMaterialValidation.h").read_text(encoding="utf-8")
        schema = self.source("panels/materialSchema.ts")
        for key in ("texture", "normalMap", "emissiveMap", "teamMask",
                    "metallicRoughnessMap", "occlusionMap"):
            with self.subTest(key=key):
                self.assertIn('key=="'+key+'"', validation)
                self.assertIn("key:'"+key+"'", schema)
        self.assertIn("studioMaterialTextureKey(key)", native)
        self.assertIn("catalog.byGuid(value.str())", native)
        self.assertIn('asset->importer != "texture"', native)
        self.assertIn('str("content/" + asset->path)', native)
        self.assertIn('std::move(validatedPatch)', native)

    def test_asset_moves_repair_material_paths_using_guid(self):
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("oldPathByGuid.emplace(asset.guid,asset.path)", native)
        self.assertIn("relocated.emplace(\"content/\"+found->second,\"content/\"+asset.path)", native)
        self.assertIn("editor::saveMaterialLibrary(ws_,library", native)
        refs = (ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        self.assertIn("edits.emplace_back(name, std::move(candidate))", refs)
        self.assertIn("studioRemapMaterialReferences(library,relocated)", native)
        self.assertIn("studioCollectAssetReferences(", native)
        self.assertIn('method == "studio.assetReferences"', native)
        self.assertIn('"view.model"', refs)
        self.assertIn("ref.writable = writable", refs)
        self.assertIn("repairedMaterialReferences", native)
        self.assertIn("const std::function<void()> commitHook", native)
        self.assertIn("pendingAssetTransfer_->apply(token,decisions,commitHook)", native)
        assets = self.source("components/AssetsPanel.tsx")
        self.assertIn("response.repairedMaterialReferences", assets)

    def test_internal_move_rolls_back_file_and_meta_before_commit(self):
        transfer=(ROOT/"apps/studio-native/StudioAssetTransfers.cpp").read_text(encoding="utf-8")
        api=(ROOT/"apps/studio-native/StudioAssetTransfers.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        ui=self.source("components/AssetsPanel.tsx")
        self.assertIn("const std::function<void()>& beforeCommit", api)
        self.assertIn("if (type_==StudioTransferType::Move)", transfer)
        self.assertIn("if (beforeCommit) beforeCommit();", transfer)
        journal=(ROOT/"apps/studio-native/StudioMoveJournal.h").read_text(encoding="utf-8")
        self.assertIn("journal.begin(journalOps)", transfer)
        self.assertIn("journal.restoreOrCleanup()", transfer)
        self.assertIn("journal.commit()", transfer)
        self.assertIn("operations.rbegin()", journal)
        self.assertIn("fs::rename(destination,source)", journal)
        self.assertIn("materials.backup", journal)
        self.assertIn("committed.json", journal)
        self.assertIn("pending.json", journal)
        self.assertIn("StudioMoveJournal::recover(project)", native)
        self.assertIn("StudioMoveJournal::recover(path)", native)
        self.assertIn("std::rethrow_exception(failure)", transfer)
        self.assertIn("StudioAssetCatalog catalog(project_); catalog.rebuild();", transfer)
        self.assertIn("Internal move only supports import, keep-both or skip", transfer)
        self.assertIn("source==='external' && !item.directory", ui)
        self.assertIn("editor::saveMaterialLibrary(ws_,library", native)

    def test_clearing_texture_reference_does_not_validate_empty_game_path(self):
        material_ops=(ROOT/"apps/editor/core/MaterialOps.cpp").read_text(encoding="utf-8")
        self.assertIn("!mat.getStr(k).empty()", material_ops)

    def test_shared_reference_registry_inventory_and_usage_panel(self):
        registry=(ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        ui=self.source("components/AssetImportSettings.tsx")
        types=self.source("types.ts")
        self.assertIn("studioCollectAssetReferences", registry)
        self.assertIn("studioRemapMaterialReferences", registry)
        self.assertIn('append("rules.json", name, "view.model"', registry)
        self.assertIn("kStudioTextureFields", registry)
        self.assertIn('method == "studio.assetReferences"', native)
        self.assertIn("studioReferenceGraphJson(", native)
        self.assertIn("studio.assetReferences", ui)
        self.assertIn("interface StudioAssetReference", types)

    def test_move_preview_marks_readonly_dependencies_and_guards_commit(self):
        registry=(ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        panel=self.source("components/AssetsPanel.tsx")
        types=self.source("types.ts")
        self.assertIn("studioMoveReferenceImpact", registry)
        self.assertIn("ref.path[source.size()]=='/'", registry)
        self.assertIn('preview.set("references",studioMoveReferenceImpact(preview,dependencies))', native)
        self.assertIn("if (reference.getBool(\"writable\")) continue;", native)
        self.assertIn("if (action!=\"skip\")", native)
        self.assertIn("read-only reference(s) block this move", panel)
        self.assertIn("disabled={busy||blocked.length>0}", panel)
        self.assertIn("references?: Array<{ id: number", types)

    def test_import_settings_lock_preserves_asset_context(self):
        assets = self.source("components/AssetsPanel.tsx")
        settings = self.source("components/AssetImportSettings.tsx")
        self.assertIn("pinnedAssetPath", assets)
        self.assertIn("const detailPath=pinnedAssetPath ?? selected;", assets)
        self.assertIn("locked={pinnedAssetPath!==null}", assets)
        self.assertIn("onToggleLock={toggleAssetPin}", assets)
        self.assertIn("setPinnedAssetPath(null)", assets)
        self.assertIn("aria-pressed={locked}", settings)
        self.assertIn("Lock import settings", settings)

    def test_shader_graph_pointer_drag_accounts_for_editor_zoom(self):
        graph = self.source("panels/ShaderGraphPanel.tsx")
        self.assertIn("document.documentElement.style.zoom", graph)
        self.assertIn("(event.clientX-current.x)/zoom", graph)
        self.assertIn("(event.clientY-current.y)/zoom", graph)

    def test_native_move_recovery_integration_targets_exist(self):
        cmake=(ROOT/"apps/studio-native/CMakeLists.txt").read_text(encoding="utf-8")
        cases=(ROOT/"apps/studio-native/tests/StudioMoveJournalTests.cpp").read_text(encoding="utf-8")
        transfer=(ROOT/"apps/studio-native/tests/StudioAssetTransferTests.cpp").read_text(encoding="utf-8")
        self.assertIn("add_test(NAME studio_move_journal",cmake)
        self.assertIn("StudioMoveJournal::recover(project)",cases)
        self.assertIn("journal.commit()",cases)
        self.assertIn("Studio recovery collision", (ROOT/"apps/studio-native/StudioMoveJournal.h").read_text(encoding="utf-8"))
        self.assertIn("injected material write failure",transfer)
        self.assertIn("assert(replaceRejected)",transfer)

    def test_scene_camera_navigation_is_editor_only(self):
        web=self.source("components/ViewportPlaceholder.tsx")
        types=self.source("types.ts")
        renderer=(ROOT/"apps/studio-native/mac/StudioMetalScene.cpp").read_text(encoding="utf-8")
        host=(ROOT/"apps/studio-native/mac/Main.mm").read_text(encoding="utf-8")
        panel=self.source("panels/StudioPanelRenderer.tsx")
        for kind in ("orbit","pan","look","move","focus","reset"):
            self.assertIn("kind==\""+kind+"\"",host)
        self.assertIn("void StudioMetalScene::pan(",renderer)
        self.assertIn("void StudioMetalScene::look(",renderer)
        self.assertIn("camera_.target=position-fromTarget*camera_.distance",renderer)
        self.assertIn("void StudioMetalScene::move(",renderer)
        self.assertIn("void StudioMetalScene::focusSelected(",renderer)
        self.assertIn("void StudioMetalScene::resetView()",renderer)
        self.assertIn("event.button===1",web)
        self.assertIn("window.addEventListener('keyup',keyup)",web)
        self.assertIn("window.addEventListener('blur',blur)",web)
        self.assertIn("inFlight=true",web)
        self.assertIn("requestAnimationFrame(tick)",web)
        self.assertIn("event.button===2?'look'",web)
        self.assertIn("['w','a','s','d','q','e']",web)
        self.assertIn("kind:'focus'",web)
        self.assertIn("kind:'reset'",web)
        self.assertIn("kind: 'move'",types)
        self.assertIn("Focus selected Scene object",panel)

    def test_global_zoom_persists_and_resizes_native_viewport(self):
        app = self.source("App.tsx")
        self.assertIn("aiverse.studio.uiScale", app)
        self.assertIn("document.documentElement.style.zoom", app)
        self.assertIn("window.dispatchEvent(new Event('resize'))", app)
        self.assertIn('min="75" max="200" step="5"', app)
        viewport = self.source("components/ViewportPlaceholder.tsx")
        self.assertIn("window.addEventListener('resize', update)", viewport)
        self.assertIn("getBoundingClientRect()", viewport)


if __name__ == "__main__":
    unittest.main()
