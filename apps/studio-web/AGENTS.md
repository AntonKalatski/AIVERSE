# AIVERSE Studio Web — AI authoring rules

## Approved visual direction — do not reinterpret
**The selected design is the FIRST cinematic concept, approved 2026-11-20.** It has a dominant real 3D Scene, premium layered dark-navy editor surfaces, blue/periwinkle focused highlights, softly raised panels, a practical Hierarchy/Inspector and a bottom Assets grid. The later stricter/flat mockup was **not** selected.

Before editing any new or existing panel, read:
- `docs/studio-visual-direction.md` — exact design/visual goal.
- `docs/studio-ui-agent-guide.md` — paths, library usage, generation, examples and verification.
- `docs/studio-design-system.md` — real shared React/CSS API.
- `skills/editor/studio-ui/SKILL.md` — repeatable workflow.

Use the shared `src/ui` library. Do not hardcode another palette, fake ECS components, substitute a mock image for Renderer3D, or change Metal viewport layering to satisfy a visual mock. Keep the current frontend functional and improve it incrementally with screenshots.

## Product
This is the **editor**, not an in-game interface. Everything shared across windows is genre-independent. The old ImGui editor is temporary; do not add new UI features there.

## Fast design loop
1. Work in apps/studio-web, use `npm run dev` for Vite HMR, preview at http://127.0.0.1:4173/.
2. Implement one panel per file in src/components/. Use semantic HTML and accessible labels.
3. Read docs/studio-design-system.md and use the shared Studio UI v1 library from src/ui. New panels MUST start with StudioPanel and use StudioSection / StudioField / StudioButton / StudioEmptyState.
4. Use semantic --au-* tokens and au-* classes; NEVER paste a new palette, hard-coded hex colors or duplicate button/input styles into a panel. Use lucide-react for icons.
5. Define a typed DTO and StudioMethod in src/types.ts for new features.
6. Backend edits MUST go through src/bridge.ts and a whitelisted StudioSession C++ operation; never directly mutate project files in JS.
7. Do not fake a rendered 3D scene in the viewport. Metal Renderer3D is running in the native macOS host; future Vulkan/GLES hosts are separate. Keep native viewport/surface layout unchanged.

## Acceptance checklist for each new screen
- Looks good with 1440x900 and narrow windows; no clipping or hidden primary actions.
- Has loading, empty, error, and disabled states.
- Keyboard focus is visible, controls have labels, interactions are reversible.
- Hot reload works in browser demo; native operations are tested in macOS WebView.
- No new ImGui dependencies; no genre-specific dependencies in shared components.
- Handoff doc added: architecture/API, exact files, tests actually run, limitations, commit, next task.

## Sample agent task
"Create a new AIVERSE Studio panel for [feature] using React + TypeScript, the shared src/ui StudioPanel, StudioSection, StudioButton, StudioField, and semantic --au-* tokens with Lucide icons. Keep backend state in C++, expose only typed bridge operations, add empty/loading/errors, run npm typecheck/build, and add handoff markdown."

## Scaffold new HTML5 Studio panels
To generate and register a new dockable screen with no changes to App.tsx:

```bash
python3 tools/studio/new_panel.py --id lighting_debug --title "Lighting Debug" --area right
# optional safe preview: add --dry-run
cd apps/studio-web && npm run typecheck && npm run build && npm run test:web
```

The generator creates a component and CSS under `src/panels/generated/`, registers its ID/title/home dock in `src/nestedDock.ts`, and inserts a render case in `src/panels/StudioPanelRenderer.tsx`. After scaffolding, AI edits the generated component for the actual feature. Backend operations must be separately allowlisted and implemented in C++ StudioSession. The script does not edit or disable the legacy ImGui editor.

## Studio UI v1 is mandatory for NEW screens
- Documentation: `docs/studio-design-system.md`. Public API: `src/ui/index.ts`, visual tokens: `src/theme/tokens.css`, global styles: `src/ui/studio-ui.css`.
- Generated panels automatically use `StudioPanel`, `StudioSection`, `StudioAlert`, `StudioBadge`, `StudioEmptyState`; do not replace them with one-off CSS widgets.
- All reusable controls belong in `src/ui/StudioUI.tsx`; any new reusable control gets a documented API and a design-system contract test.
- Responsive/docking: panes must survive 240px width and 900px workspace height; no fixed-width columns that overflow.
- Import screen-specific CSS only for layout; colors, font sizes, border radii, controls and focus appearance come from the design system.
- Check `python3 -m unittest discover -s tools/studio -p 'test_*.py' -v` and `npm run typecheck && npm run build` after changing shared styles.

## Existing panels — gradual migration
The old App shell and NestedDockWorkspace use `src/ui/editor-shell.css` to inherit shared tokens without changing their event handling or native Metal geometry. Migrate each panel to true Studio UI components gradually, with macOS screenshots. Do not duplicate shell overrides in panel CSS or alter .viewport/.viewport-native rules to change the native renderer.

## Integrated MotionWeave Facial Rig preview (Stage 182, separate branch)

When working on `integration/motionweave-studio-face-20261120`,
do NOT replace `src/panels/FacialLivePreview.tsx` with a canvas,
screenshot, fake character or browser-side animation. It sends typed
`studio.facialPreviewStart/Stop/Scalar/Vector/State` calls to the
native `StudioSession`. Actual Model import, semantic 64-channel
morph bindings, IK and one parent pose writer live in
`apps/studio-native/mac/StudioFacialGpuPreview.cpp`
using `AIVERSE::MotionWeave`. The real Scene is the native Metal
sibling surface. A request accepted by C++ is not GPU-ready;
`facial.preview.ready` is emitted only after `Renderer3D`
submits a frame. Even that does not prove GPU pixel parity.
No synthetic DEMO GPU success is permitted.

Use the existing Studio UI components, tokens, dock and bridge types.
Stop preview before modifying the saved immutable rig. Keep native
project-only asset path validation and full GPU owner retirement.
This source integration is NOT a full semantic merge of all Studio
and Animation branch changes and has NOT passed native tests.
See `docs/studio-motionweave-facial-live-preview.md` and
`docs/handoff/studio-html5/68-motionweave-native-facial-preview-2026-11-20.md`.
