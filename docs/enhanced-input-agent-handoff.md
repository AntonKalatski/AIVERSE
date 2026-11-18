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

## Stage 2 — reusable action/context core + tests (implementation written; CI still pending)

**Changes on this branch**
- New \`libraries/include/aiverselib/gameplay/EnhancedInput.h\`: header-only C++20 \`aiverse::lib::gameplay::enhanced\` namespace. Legacy \`gameplay/Input.h\` is unchanged.
- New \`tests/libraries/enhanced_input_tests.cpp\`: movement vector composition/normalization, boolean key phases, hold/tap/release, same-frame tap, chord, priority override, rebind, focus cancel, analog deadzone, relative mouse delta.
- \`tests/libraries/CMakeLists.txt\`: added \`aiverse_enhanced_input_tests\` linked only to \`AIVERSE::lib_input\` and CTest \`library_enhanced_input\`; added a missing \`endif()\` for the existing \`voxel packages\` conditional at the end. Confirm this closure with CMake on next build.

**C++ API (Stage 2)**
\`\`\`cpp
#include <aiverselib/gameplay/EnhancedInput.h>
using namespace aiverse::lib::gameplay::enhanced;
InputSystem input;
input.defineAction({"Move", ValueType::Axis2D, {Modifier::normalize()}});
MappingContext gameplay{"Gameplay"};
gameplay.bind("Move", {Device::Keyboard, /* platform::Key::W */ 23}, {Modifier::swapXY()});
input.registerContext(std::move(gameplay));
input.activateContext("Gameplay", 0);       // higher priority wins; equal uses name order
input.setControl({Device::Keyboard, 23}, 1); // down
for (const ActionEvent& e : input.update(1.f/60)) {
  // e.action, e.phase (Started/Ongoing/Triggered/Completed/Canceled), e.value
}
input.deactivateContext("Gameplay");
input.cancelAll();                          // focus lost; sends Canceled and resets held controls
\`\`\`
Control codes above are **illustrative**; use \`int(platform::Key::W)\` in real platform glue rather than 23.

Available:
- \`ValueType\`: Boolean, Axis1D, Axis2D, Axis3D. \`Value{x,y,z}\`.
- \`Device\`: Keyboard, MouseButton, MouseAxis, MouseWheel, GamepadButton, GamepadAxis, Touch; \`Control{Device, code, deviceIndex=0}\`.
- \`Modifier\` factories: \`scale(x,y,z)\`, \`negate()\`, \`deadZone(t)\`, \`swapXY()\`, \`normalize()\`.
- \`Trigger\` factories: \`down()\`, \`pressed()\`, \`released()\`, \`hold(seconds)\`, \`tap(seconds)\`, \`chord(actionName)\`.
- \`MappingContext::bind(action, Control, mappingModifiers, mappingTriggers, consumes=true)\`; \`InputSystem::{defineAction,registerContext,activateContext,deactivateContext,clearContexts,rebind,setControl,addDelta,update,events,state,cancelAll}\`.
- Stable ordering: contexts high priority first; tie by context name; mappings in declaration order. Consumed controls cannot activate mappings in lower-priority contexts. Chords sample same-frame raw action levels. Relative deltas reset after update. Trigger timing uses supplied deltaSeconds (not wall-clock).
- An action may emit multiple events in a frame, e.g. Started+Triggered; Released/Tap can yield Triggered+Completed. \`ActionState.active\` is the held level; action state \`triggered\` latches a prior trigger during that hold. \`Trigger::hold\` is one-shot until release.
- \`Mapping.id\` is allocated when the context is registered; \`rebind(contextName, mappingId, newControl)\` changes one mapping.

**Verification performed**: source-level API audit and GitHub commits. Full compiler/CTest runs are **not yet performed**: GitHub remote is not reachable from the local build container; connected GitHub access is available. Run the new standalone target as the first validation step, then \`python3 tests/libraries/run.py\` + \`python3 tests/libraries/native_consumer.py\`.

**Known limitations / follow-up**
1. There is no platform bridge yet. Implement \`engine/platform/EnhancedInputAdapter.h\` next; do not mix UI text composition events with gameplay actions.
2. Mapping IDs must remain unique within contexts; future hardening should reject explicit duplicate IDs.
3. Profile JSON, persisted rebindings, runtime bindings editor and migration of existing FPS/RTS/editor are not yet included.
4. Chord prepass currently uses raw levels from active contexts (before consumption), so validate complex overlapping modal/chord scenarios.
5. Validate GCC/Clang/MSVC warning clean builds before calling Stage 2 verified.

### Next agent action
Stage 3: add a tiny adapter for \`platform::Event\` and \`platform::GamepadState\` with correct focus-loss handling and normalized mouse/gamepad mappings. Switch at least the FPS movement and fire input path to action states/events without changing \`fps::Input\` network command layout; add adapter smoke tests. Update this document immediately after that stage.

## Stage 3 — platform adapter + FPS reference migration (written; verification pending)

**Changed files**
- \`engine/platform/EnhancedInputAdapter.h\`: \`platform::EnhancedInputAdapter\`. \`onEvent(platform::Event)\` converts KeyDown/Up, MouseDown/Up, relative MouseMove axes, MouseWheel, TouchBegin/End/Cancel and FocusLost; \`pollGamepads()\` converts the 4 pads from \`platform::Gamepad.h\`; \`focusLost()\` cancels all held action states. Text/IME and UI events stay outside this layer. Polling same pad backend twice per platform frame is not supported: caller must own the poll.
- \`apps/fps/client.cpp\`: no \`std::set<Key>\` or per-key direct movement/fire logic. New \`App::configureInput()\` defines semantic FPS Actions/Context, including Move/Look/LookStick, Fire/Aim/Jump/Crouch/Sprint/Reload, inventory, weapon slots, reload, melee/block, throwable, interaction and buy. The project uses \`platform::EnhancedInputAdapter\` and reads \`ActionState\`/events before each simulation tick. One-shot Jump/Fire are queued until the next successful command submit. The authoritative \`fps::Input\` protocol, simulation and serialization are unchanged.
- Gamepad 0 is bound: left-stick move, right-stick camera look, triggers Fire/Aim, buttons A jump/B crouch/X reload/Y interact and LStick sprint.
- \`tests/libraries/enhanced_input_adapter_tests.cpp\`: keyboard, mouse button/motion/wheel, touch and mocked gamepad mapping; FocusLost cancel.
- \`tests/libraries/CMakeLists.txt\`: target/CTest \`aiverse_enhanced_input_adapter_tests\` / \`library_enhanced_input_adapter\` linked to \`AIVERSE::lib_input\` with \`GamepadCommon.cpp\`.
- \`EnhancedInput.h\`: Axis1D/2D/3D values remain unbounded (important: mouse delta in pixels cannot be clamped to [-1,1]); apply \`Modifier::normalize\` or DeadZone deliberately for normalized sticks/movement, not for mouse deltas.

**Behavioral contract**
- FPS gameplay actions are fed only while pointer-locked; clicking the main view first acquires pointer lock. Escape/focus loss cancels gameplay input immediately. This preserves the legacy gameplay capture model. A dedicated menu/input context can replace this in a later stage.
- \`InputSystem::update(dt)\` is presentation-frame-driven; action state is sampled by the fixed network/simulation command tick. Relative mouse axis applies once per frame. Gameplay state does not leak into deterministic ECS.
- Temporary FPS default mappings live in \`App::configureInput()\` until project-editable mapping assets are implemented in Stage 4. Do not duplicate these defaults for every project.
- The adapter exposes 4 gamepads. The FPS sample currently binds pad index 0 only, ready for project-specific player contexts.

**Verification:** Source inspection and tool commits only; no compiler run or CTest run yet. Review new FPS mapping code under Clang, particularly include/link dependencies and every transition on lost pointer lock. CI on a draft PR is the next validation path.

**Next agent**
Stage 4 must add validated action/context JSON schemas and per-user rebinding profile persistence, without changing \`rules.json\` gameplay data contracts. Update input skill and catalog, install tests for invalid JSON, save/load roundtrip and override priorities. Then update this handoff immediately. Stage 5: migrate RTS/editor to configurable contexts, expose actions UI, verify all sample projects.
