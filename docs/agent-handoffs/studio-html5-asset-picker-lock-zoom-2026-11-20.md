# Studio HTML5 — asset references, Inspector lock, global UI zoom (2026-11-20)

## Branch and status
- Repository: chromealex/AIVERSE
- Branch: `feature/studio-html5`.
- Starting point: existing Studio HTML5 asset browser with project-relative `content/`, native asset catalog, GUIDs, and `application/x-aiverse-content-path` drag MIME.
- Last implementation commit at handoff: `714723e0c610c19d48f67ccd19ebdd4085781000` (before this documentation commit).
- This stage was edited through the GitHub connector. **No local npm/CMake/CTest build was executed.** Do not label it verified until local checks pass.

## Implemented
1. `apps/studio-web/src/components/AssetReferencePicker.tsx` and `.css`
   - Reusable modal over `studio.assets`, with path/name/GUID search, optional asset-kind filtering, clear, Escape and overlay-close.
   - Accepts drag of project/content asset from `AssetsPanel` using the existing MIME; rejects OS drops and unsafe paths.
   - Selects GUID when catalog entry provides it; otherwise writes content-relative path. API: `<AssetReferencePicker value onChange label kinds? disabled? allowClear? />`.
   - Asset catalog is native authoritative; no browser filesystem IO or duplicated asset registry.
2. `propertyLogic.ts` / `PropertyField.tsx`
   - Added `FieldDescriptor.type='asset'` and optional `assetKinds`. Inspector fields explicitly marked asset use the picker. Existing text/number/boolean behavior unchanged.
3. `App.tsx` / `StudioPanelRenderer.tsx`
   - Inspector lock snapshots selected entity and its property values, independent of later global scene selection changes.
   - Writes target pinned entity ID; pinned displayed values update on successful commits. Unlock returns to live selection.
   - Editor settings control in titlebar: global `uiScale` range 75–200%, step 5%, persists in localStorage; document-root CSS zoom scales dock, panels, dialogs and content. Fires resize to sync native viewport surface.
4. `ui/editor-shell.css`: settings UI styling.

## Important integration work still pending
- **Native schema:** Current native `studio.snapshot` may not emit fields with type `asset`; register all actual asset-valued object-reference properties at source and verify `studio.setProperty` accepts GUID/path representation for each. This feature does not magically convert arbitrary text keys into asset fields.
- **Every reference site:** Audit materials, shader graphs, scene components, animation, VFX, lighting and future migrated panels. Use the shared picker for all explicit asset references, not handcrafted text inputs. Current materials authoring exposes PBR scalar values only; do not pretend texture slots exist before backend support.
- **Drag-to-GUID:** Current direct drag can fall back to a path when the picker has not loaded the catalog. If native schema requires GUID, resolve dropped asset via native `studio.assetByGuid`/catalog lookup and reject unresolved drops instead of writing a path.
- **Lock semantics:** Inspector is locked against external selection, but the snapshot of pinned properties is not automatically refreshed from native mutations made by another panel. A future native property-by-ID fetch method would allow precise refresh.
- **Other dock panels:** Add lock toggles only where external selection should not mutate panel context (preview, shader graph, material editor as needed). Do not lock panes that never follow selection.
- **Zoom QA:** Verify macOS WKWebView composited Metal viewport alignment, native hit-testing, drag boundaries, scrollbars, modal positions, high-DPI, 75%/100%/150%/200% and persistence across restart. CSS zoom must not scale gameplay world-space itself.

## Validation required on developer Mac
```sh
cd apps/studio-web
npm ci
npm run typecheck
npm run test:web
npm run build
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_*.py'
```
Manual QA: select an asset by search, drag image/model from Assets, clear asset, reject unmatched kind; lock Inspector A then select B in Hierarchy, edit A and unlock; set UI zoom to each boundary and verify native Scene frame and pointer alignment.

## Next step for an agent
Run typecheck and resolve failures, then implement native explicit asset reference descriptors and adopt the picker panel-by-panel. Document each stage with actual checks, affected files and an immutable SHA. Do not merge to main before native QA.
