# AIVERSE Studio Web

**Status: Stage 1 preview only.** This is a new UI for the **editor**, not a replacement for the engine's in-game UI. Native renderer/engine integration is not yet available, and the existing ImGui editor remains the production editor.

## Run
\`\`\`sh
cd apps/studio-web
npm install
npm run typecheck
npm run dev
# open http://127.0.0.1:4173/
\`\`\`

\`npm run build\` produces \`dist/\` with **relative** asset paths for future WKWebView packaging.

## AI-first screen authoring
- Build a panel in \`src/App.tsx\` or extract its component into \`src/components/<Name>.tsx\`.
- Use design tokens/classes in \`src/styles.css\`; prefer semantic HTML and accessibility.
- Reuse \`PanelTitle\`, controlled inputs, typed snapshots and bridge methods.
- Never call the engine from React components directly; use \`src/bridge.ts\`.
- Ask AI to make a new screen, run Vite HMR and inspect in a browser without recompiling C++.
- Keep UI data in TypeScript DTOs; engine state/undo belongs to native Workspace.

## IPC v1
WKWebView handler: \`window.webkit.messageHandlers.aiverse.postMessage({id, method, params})\`.
Native replies via \`window.__aiverseReceive({id, result})\` or \`{id,error}\`.
Events via \`window.__aiverseReceive({event:'workspace.changed',payload:{...}})\`.

The current browser preview uses a **DEMO-only** in-memory adapter. Do not treat its edits as project edits.

## Migration rules
1. No genre-specific code in the UI infrastructure.
2. No ImGui dependencies in new files.
3. Renderer remains native (Metal/Vulkan/GLES); no bitmap streaming every frame.
4. Security: only trusted locally packaged page allowed to invoke native editor operations.
5. Keep old editor functional until verified feature parity.
6. Each stage must add a handoff markdown in \`docs/handoff/studio-html5/\`.
