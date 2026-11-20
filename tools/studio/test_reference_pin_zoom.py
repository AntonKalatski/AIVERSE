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
        self.assertIn("asset.guid || asset.path", picker)
        self.assertIn("Search assets", picker)
        self.assertIn("event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)", picker)
        self.assertIn("result.files.find(item => item.path === path)", picker)
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
        panel = self.source("panels/StudioPanelRenderer.tsx")
        self.assertIn("toggleInspectorPin", panel)
        self.assertIn("aria-pressed={inspectorPinned}", panel)

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
