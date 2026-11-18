# AIVERSE Enhanced Input — agent handoff (living document)

Branch: `feature/aiv990_enhanced_input` (base `main`). Owner: input subsystem. Updated 2026-11-18.

**Mandatory workflow:** after EVERY stage update this file before moving on. Describe changed files and public API, architecture/invariants, exactly which build/tests ran (and results), remaining work, and precise next-agent instructions. No stage is considered complete without its handoff update. Keep previous-stage summaries so an agent can reconstruct decisions.

## Stage 1 — audit and design (complete)

### Existing code
- `libraries/include/aiverselib/gameplay/Input.h`: `InputPhase`, `InputEvent`, `Binding`, `resolveInput` (single best priority match, stable ID tie-break), integer `deadzone`; also selection/control-groups/drag gestures. This is the old public API: **do not remove or break**.
- `engine/platform/Platform.h`: platform-normalized `Event` for KeyDown/Up, MouseDown/Up, MouseMove (delta), Wheel, Touch, FocusLost. `Key` enum and modifiers.
- `engine/platform/Gamepad.h`: `pollGamepads`, four standard-layout pads, `PadButton`, `PadAxis`, button edges, per-axis floats.
- `apps/fps/client.cpp`: directly maintains pressed `std::set<platform::Key>`, weapon/action one-shots, mouse look and fire; builds `fps::Input` commands per simulation tick.
- RTS in `apps/client` and editor input in `apps/editor` have their own event/UI handling. Avoid mass-rewriting all clients at once; isolate input evaluation from deterministic sim commands.
- `skills/ui/aiverse-input/SKILL.md` and `libraries/catalog.json` describe the current `input` package, currently v0.1.1.
- `tests/libraries/gameplay_tests.cpp` tests the legacy library API. Preserve these tests.

### Target architecture (Unreal Enhanced Input-inspired, **not** Unreal binary/API compatibility)
`Platform events/gamepads -> normalized device controls -> active prioritized Mapping Contexts -> mapping Modifiers -> Action modifiers/triggers -> Action event states -> game/editor/UI callbacks -> validated deterministic Commands -> ECS systems/simulation.`

Provide:
1. Named Input Actions with Bool / Axis1D / Axis2D / Axis3D values; consistent normalized vectors.
2. Mapping Contexts activated/deactivated at runtime with priorities. Higher active contexts may consume controls; UI and modal contexts override gameplay without changing bindings.
3. Mapping modifiers (scale, negate/invert, scalar deadzone, axis swizzle, normalize) and triggers (press, release, hold, tap, chord) with explicit state edges (Started, Ongoing, Triggered, Completed, Canceled).
4. Keyboard, mouse buttons, mouse move/wheel, gamepad axes/buttons; pointer lock, focus loss; no sim-specific representation here.
5. Rebindable profiles and serialization, without overriding engine `game.json` and without project-specific hardcodes.
6. Native C++ library access via existing `AIVERSE::lib_input` target; no Lua requirement.
7. Sufficient tests and example integration in FPS, then incremental RTS and editor migration.

### Invariants
- Presentation-side values may be float; **simulation never reads device state**, only validated/tick-stamped Commands (integer/Fixed). Do not duplicate input state into authoritative ECS. ECS systems consume commands produced from actions; local input resources may be presentation-only.
- Input lost focus, modal capture, or context removal must release held state, not leave buttons stuck.
- At equal priority, processing must be stable and documented. Chords must use action states, not ad hoc raw-key checks.
- Do not break `Input.h` legacy classes, gamepad conventions or existing serialized FPS/RTS command layouts.
- Per-project actions and contexts are data; the engine must not hardcode game-specific schemes.
- Tests include edge transitions, timing, chord precedence, deadzone/inversion, multi-device, context suppression, rebinding and focus loss.

### Stage plan
- **Stage 2:** implement the pure reusable Enhanced Input core, unit tests, native example; update this handoff.
- **Stage 3:** implement platform event/gamepad adapter and integrate representative FPS controls; update this handoff.
- **Stage 4:** profile JSON/asset loading and rebinding support, agent skill/catalog updates; update this handoff.
- **Stage 5:** RTS/editor context integration, cross-platform validation and remaining compatibility; update this handoff. (May require additional PR if editor migration would collide with active editor work.)

### Stage 1 verification
- Inspected repo over GitHub connector (main branch). No code or build changes in this stage.
- The container cannot reach GitHub directly (DNS unavailable); use the connected GitHub tools for reads/writes. For compilable standalone header tests, local compiler is available. Full project build on user's Mac or CI not yet performed.

### Next agent action
Implement Stage 2 in `libraries/include/aiverselib/gameplay/EnhancedInput.h`. Keep the legacy `Input.h` intact, add new focused tests under `tests/libraries` (or standalone no-engine test if appropriate), then document exact APIs and verification here before proceeding.
