# VIVID subsystem agent rules

Read `AGENTS.md`, `docs/vivid.md` and `docs/agent-handoffs/vivid.md` **before each stage**.
Current feature branch: `feature/vivid-digital-humans`. Other agents edit models, shaders, batching, animation and Prisma; coordinate shared files, do not overwrite their work, and sync main at integration boundaries.

## Ownership and contract
- Own `engine/character/vivid/`, `tests/vivid/`, `docs/vivid.md`, `docs/agent-handoffs/vivid.md`.
- Core facial module must remain C++20, native, portable and independent of rendering, UI and genre. Public API `aiverse::vivid`, linked as `AIVERSE::vivid`.
- Face channels use the fixed, indexed 52 ARKit-style names in `Face.h`; changing order is an asset/network compatibility concern. Do not silently renumber.
- Layers Tracking -> Emotion -> Speech are masked and blended in order; animation sampling explicitly calls `advance(dt)` or `snap()`.
- All numeric external inputs must reject NaN/Inf, saturate normalised controls, and avoid unintended state changes on errors.
- No authoritative ECS gameplay mutations, no Unreal runtime dependency, no bundled third-party human scans or MetaHuman assets.
- Later render mapping must preserve existing AnimationGraph/Model semantics and respect `kMaxMorphTargets=8` until a separately approved extended deformer lands.

## Every-stage protocol
1. Implement one bounded stage and commit changes. Keep public API and compatibility documented.
2. Run scoped tests when a build host is available; only describe tests actually run.
3. Add a **separate** handoff commit on this same branch with actual implementation SHAs, tests, warnings, unfinished parts and the next action.
4. If no build is available, mark full native tests NOT RUN. Pure source checks are not a substitute.
5. Update `docs/vivid.md` status when advancing beyond the design stage.
