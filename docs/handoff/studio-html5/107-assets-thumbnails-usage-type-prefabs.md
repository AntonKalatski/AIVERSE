# 107 — Asset thumbnails, usage, unit types as prefabs (stage 4 of the editor plan)

## Done
- **Thumbnails in the Assets grid** (`components/AssetThumb.tsx`, `studio.assetThumbnail {path}`): images are
  scaled by AppKit; models are rendered **offscreen** by the Scene (160² BGRA8 target + depth, one per frame
  before the scene pass, `StudioMetalScene::renderThumbnail`, `Device::captureTexture` → PNG) and announced
  with `asset.thumbnail`. Cache: `~/Library/Caches/AIVERSE/thumbnails/<fnv(project|path|size|mtime)>.png` —
  never inside the project. Tiles request only when visible (IntersectionObserver).
- **Find References** (asset context menu) and **Unused** filter (toolbar): `studio.assetUsage` scans the
  project's text data (json, lua, rml, rcss, gltf, .aiverse; skips Builds/logs/.git…): mentions of
  `content/<path>` with file:line, and content files no file names (glTF relative uris count). Paths built in
  scripts are not followed — the UI says "not referenced by name". `StudioAssetUsage.h`.
- **Unit types as prefabs**: Rules type rows drag into the Scene (`aiverse-type:<id>` →
  `placeType` → `studio.entityCreate`, owner 0 for resources/invulnerable, else 1). Inspector's Unit
  component links "Edit type … in Rules →" (selects the type in the Rules panel).
- **Title bar**: a grid (left | play controls | right) keeps Play/Pause/Step centred while both sides fit and
  pushes them instead of overlapping (the new Character workspace tab was hidden under them).

## Verified on the Mac (private copy of lua_lab + 4 truck .glb + 2 textures)
- Vehicles folder showed rendered truck thumbnails, textures folder image thumbnails; the Scene kept
  rendering normally; Unused listed all 6 (nothing references them). Session test covers assetUsage.
- Bug found and fixed: the thumbnail path regex was unterminated (`[^\]`), every request failed.
- Not checked on screen: Find References dialog and type drag (context menus / cross-panel drags cannot be
  driven in the background).
