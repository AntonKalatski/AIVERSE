# AIVERSE Studio Web

**Status: Stage 5 experimental HTML5 editor.** This is a new UI for the **editor**, not a replacement for the engine's in-game UI. Native Workspace connectivity is available in the experimental macOS WKWebView host, while the GPU viewport is not yet integrated, and the existing ImGui editor remains the production editor.

## Run
```sh
cd apps/studio-web
npm install
npm run typecheck
npm run dev
# open http://127.0.0.1:4173/
```

`npm run build` produces `dist/` with **relative** asset paths for future WKWebView packaging.

## AI-first screen authoring
- Build independent React panels in `src/components/<Name>.tsx`; keep `src/App.tsx` as the host/composition layer.
- Change global palette/spacing in `src/theme/tokens.css`, not by changing every color manually.
- Read `AGENTS.md` before generating a new screen or component.
- Use design tokens/classes in `src/styles.css`; prefer semantic HTML and accessibility.
- Reuse `PanelTitle`, controlled inputs, typed snapshots and bridge methods.
- Never call the engine from React components directly; use `src/bridge.ts`.
- Ask AI to make a new screen, run Vite HMR and inspect in a browser without recompiling C++.
- Keep UI data in TypeScript DTOs; engine state/undo belongs to native Workspace.

## IPC v1
WKWebView handler: `window.webkit.messageHandlers.aiverse.postMessage({id, method, params})`.
Native replies via `window.__aiverseReceive({id, result})` or `{id,error}`.
Events via `window.__aiverseReceive({event:'workspace.changed',payload:{...}})`.

The current browser preview uses a **DEMO-only** in-memory adapter. Do not treat its edits as project edits.

## Migration rules
1. No genre-specific code in the UI infrastructure.
2. No ImGui dependencies in new files.
3. Renderer remains native (Metal/Vulkan/GLES); no bitmap streaming every frame.
4. Security: only trusted locally packaged page allowed to invoke native editor operations.
5. Keep old editor functional until verified feature parity.
6. Each stage must add a handoff markdown in `docs/handoff/studio-html5/`.

## Create a new screen with AI
From the AIVERSE repository root:

```bash
python3 tools/studio/new_panel.py --id world_stats --title "World Statistics" --area right
```

The panel appears in the HTML5 Panels menu after Vite reload, with its TSX/CSS in `apps/studio-web/src/panels/generated/`. No C++ compilation is needed for the UI layout, but operations that mutate the engine still require explicit validated native bridge APIs.
