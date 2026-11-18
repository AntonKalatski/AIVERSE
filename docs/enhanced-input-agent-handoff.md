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

## Stage 4 — validated project assets + user profiles + agent skill/catalog (written; CI pending)

**Code**
- \`engine/platform/EnhancedInputConfig.h\`: JSON \`Asset\` / \`ContextConfig\` / \`Profile\` / \`Rebind\` models and schema-version 1. \`readAsset\`, \`writeAsset\`, \`installAsset\` (stages a new InputSystem before replacing current); \`readProfile\`, \`writeProfile\`, \`applyProfile\` (validates every target id before rebinding). Uses the existing \`engine/data/Json.h\`, string key/button/axis names from platform enums, limits on objects/counts/numerics and helpful path errors. **Host/presentation only**, not linked into core deterministic ECS.
- \`apps/fps/client.cpp\`: loads optional complete \`<project>/input/actions.json\` (invalid or missing -> built-in mapping fallback), then local profile under \`platform::userDataDir()/enhanced-input-<stable FNV path hash>.json\`. \`App::rebindInput(context,id,Control)\` is a local host/UI hook that updates the profile and writes a temp+rename file; \`platform::syncUserData()\` after write. No user-visible binding editor yet.
- \`tests/libraries/enhanced_input_config_tests.cpp\` and its CMake target/CTest \`library_enhanced_input_config\`: JSON asset/profile roundtrip, chord config, stable code names, unknown IDs/duplicate bindings/unsupported code/version fail closed.
- \`docs/enhanced-input.md\`: user-facing architecture, examples, complete JSON schema, profile format, native/ECS usage and validation.
- \`skills/ui/aiverse-input/SKILL.md\`: rewritten for Action/Context API and requires a doc handoff after each step.
- \`libraries/catalog.json\`: existing input package 0.1.1 -> 0.2.0, dependency pins for new headers/docs and descriptions. Legacy source remains \`gameplay/Input.h\`.
- \`AGENTS.md\`: added Enhanced Input ownership row and per-stage agent handoff rule.

**Supported contract:** asset path is a convention for the standalone FPS sample, not an implied existing \`rules.json\` or \`game.json\` option. If present, the asset contains the **entire** desired FPS action set (not a partial merge). Pure header can be consumed by a native C++ project via \`AIVERSE::lib_input\`. The platform JSON adapter requires engine JSON includes. Keep \`input/actions.json\` inside each concrete project's own repo, not in the engine repo.

**Verification status:** implementation and documentation committed; Linux/Apple/MSVC compiler and CTest have not yet returned success. Draft PR #20 triggers CI; prior CI runs were canceled due new commits. Explicitly investigate final CI results and do not merge until green. The repo is too large to pull in the current tool container (network DNS is unavailable); validate through Github Actions and user's local Mac build.

**Next agent: Stage 5**
1. Fix any compiler/test failures from draft PR #20 CI. Check new header for GCC/MSVC aggregate warnings and \`tests/libraries/CMakeLists.txt\` nesting. Run isolated library targets and full engine build.
2. Add editor input settings (Input Actions / Mapping Contexts / modifier/trigger UI, capture key to rebind, priority and conflict warnings), using \`docs/editor-windows.md\` and ImGui/RmlUi event capture correctly.
3. Integrate RTS \`apps/client\` and in-editor hosted game input with higher-priority UI/Modal/Viewport contexts, preserving text input/IME and the existing RTS Commands. Treat gameplay actions as client->Commands, not sim mutations.
4. Test pad disconnect, held key during modal activation, duplicate explicit IDs, chord consumption precedence, same-frame tap, focus loss; update reference docs, tests, skill/catalog and this handoff after the next stage. Keep unrelated editor changes minimal.

## Stage 5 — inspector FPS/ECS merge (2026-11-18)

**Integrated parents:** Enhanced Input `247d9fb4344abf9b7988e367ce83f31a9afe6863` and `feature/universal-entity-inspector-fps` `26eb4d914fc65f336c9e843db198f2316f6f14a2`. Two-parent merge preserves both histories. Resolved tree is inspector HEAD plus Enhanced Input files, not a reset to `main`.

**Conflict resolutions:** `AGENTS.md` retains inspector's ECS/renderer rules and adds input ownership and stage-by-stage handoff. `libraries/catalog.json` updates only `input` package 0.2.0 while retaining all inspector library entries. `tests/libraries/CMakeLists.txt` retains inspector test targets and appends three enhanced input test targets, dropping the redundant additional `endif() # voxel packages` from the input branch. FPS client kept from input branch; inspector did not modify that path after common base. Do not overwrite the merged inspector/editor or ECS sources with outdated main.

**Validation:** source and tree comparison over GitHub connector; no native compile or CTest yet (build container cannot resolve GitHub DNS). Run CMake/CTest on the merged branch, audit the FPS client against current `modules/fps`, then add editor/RTS contexts and runtime binding settings. Update this handoff after each stage before proceeding.

## Stage 6 — runtime ID / touch hardening (2026-11-18)

**Changes:** `EnhancedInput.h::InputSystem::registerContext` now rejects duplicate explicit mapping IDs, unknown referenced action names and invalid MAX ids before modifying registered contexts. Automatic mapping IDs skip all authored IDs. `EnhancedInputAdapter.h` reuses freed touch slots (0–31), ignores repeated begin for the same touch ID and ignores fingers over the supported 32-slot maximum without wrapping. `enhanced_input_tests.cpp` adds registration regression checks.

**Compatibility:** legacy `gameplay/Input.h` is unchanged. Action values, FPS/RTS network Commands and profile schema 1 are unchanged. Native C++ projects may continue linking `AIVERSE::lib_input`.

**Verification:** source-level assertions reviewed; merged CI was queued when checked. No compiler or CTest run yet. Continue checking GitHub Actions or run the isolated `aiverse_enhanced_input_tests` on a machine with the checkout.

**Next agent:** finish the editor Input Actions/Contexts UI and integrate safe project asset editing, then migrate RTS/hosted game UI arbitration and verify. Add a document section after each stage.

## Stage 7 — editor authoring window (2026-11-18)

**Code:** Added `apps/editor/windows/InputWindow.cpp` registered as window ID `input` in the existing Logic group. Opens the project's optional `input/actions.json`; supports Actions and typed values, context enable/priority, mappings, key capture, modifiers/triggers, and warnings for duplicate controls. Saves only schema-valid full assets via `enhanced_config::writeAsset/readAsset`; temporary write and backup file protect existing JSON. Existing per-user override profiles remain separate. `EditorWindow::captureGlobalShortcuts` and `Editor::handleShortcuts` suppress F5/undo while key capture is armed; `update` cancels capture when dock tab loses front position. `CMakeLists.txt` links `aiverse_editor` directly to `AIVERSE::lib_input`; the existing editor window GLOB picks up the new source.

**Files:** `apps/editor/windows/InputWindow.cpp`, `apps/editor/EditorWindow.h`, `apps/editor/Editor.cpp`, root `CMakeLists.txt`, `docs/enhanced-input.md`, this handoff.

**Validation:** GitHub source updates only; no compiler/CTest results. Verify ImGui named key enum range and project asset backup/rename path with Clang/macOS, GCC/Linux and MSVC/Windows. Ensure Input Actions window works with FPS and RTS; remapping F5 must not begin Play. The editor does not generate complete FPS defaults automatically, so users must build the full scheme when creating an asset.

**Next agent:** migrate the common RTS client hotkeys to semantic actions, preserve `scriptCommandKey`/control groups/hosted view UI handling, add focused tests and run full CTest/CI before marking complete. Update this handoff after Stage 8.

## Stage 8 — RTS semantic shortcut migration (2026-11-18)

**Code:** `apps/client/ClientInputRouting.h` defines default semantic Actions `AttackMove`, `Patrol`, `Repair`, `Reclaim`, `Stop`, `Hold`, `FocusCamera` under Mapping Context `RTS.Gameplay`. The handler accepts platform KeyDown/KeyUp only, raises `Triggered` events, and immediately cancels held state when RmlUi/modal/text/paused UI captures input, or on focus loss. `GameClient::event` now consumes those semantic actions instead of hardcoded A/P/R/E/S/H/Space, preserving `scriptCommandKey` precedence and the existing command implementations. `GameClient::initStep(0)` optionally loads a full project-authored `input/actions.json` into that same local input system, falling back to defaults on a parse/config error. Core simulation, FPS network input and RTS Command wire formats are unchanged. Control groups, selection pointer gestures, WASD camera scrolling and other specialized client inputs remain on their current paths for compatibility.

**Files:** `apps/client/ClientInputRouting.h`, `apps/client/GameClient.h`, `apps/client/GameClient.cpp`, `tests/libraries/enhanced_rts_input_tests.cpp`, `tests/libraries/CMakeLists.txt`, `docs/enhanced-input.md`, this handoff.

**Test cases added:** pressed-only RTS actions, KeyUp and hold/repeat, UI modal/focus cancel and higher-priority context overriding a shortcut. Full build/CTest still pending, and a real RTS example should verify script keys, ctrl groups and hosted-client input.

**Next agent:** complete default camera/mouse action migration where it is safe, add user-facing rebinding profile UI (separate from authored assets), and verify compiler, CTest, macOS hosted FPS/RTS and other platforms. Never claim end-to-end validation from source-level review.

## Stage 9 — refresh inspector branch after concurrent ECS/test changes (2026-11-18)

`feature/universal-entity-inspector-fps` advanced by 8 commits after the Stage 5 merge, from `26eb4d9` to `dd711f5`. Compared all 13 changed paths: ECS scene hierarchy, projectile systems/tests, engine test runner, skills catalog, validation workflows and developer docs. None overlaps this input feature's files. Applied those 13 newer paths on top of the current Enhanced Input worktree as a second two-parent merge, retaining both parents and input changes.

**Verification:** GitHub API changed-path comparison; zero overlapping paths, no local CMake/CTest result; CI remained queued during source review. This merges only the known 8 new upstream commits; repeat comparison before PR finalization if the inspector branch moves again.

**Next agent:** compile editor/FPS/RTS input targets, run all library input tests and inspect CI. Continue parity work for camera/pointer/gamepad actions only after capturing results in this handoff.

## Stage 10 — in-game FPS rebinding/settings (2026-11-18)

**Implementation:** The standalone FPS reference client now forwards platform events to its existing ImGui layer and provides an `Input Settings` panel toggled with F10. Opening releases pointer lock and cancels held gameplay input. The panel enumerates all registered Mapping Contexts (including inactive), shows semantic Action-to-Control bindings, collision notices and live key/mouse/gamepad capture. Rebindings call the existing `App::rebindInput`, persisting only user profile overrides; authored `input/actions.json` stays unchanged. Escape cancels capture/ closes settings, F10 toggles, clicking the viewport reacquires pointer lock. Added the read-only `InputSystem::registeredContexts()` enumeration API and a core test assertion.

**Files:** `apps/fps/client.cpp`, `libraries/include/aiverselib/gameplay/EnhancedInput.h`, `tests/libraries/enhanced_input_tests.cpp`, `docs/enhanced-input.md`, this handoff.

**Validation:** code/asset API inspected; FPS visual/interactive behavior and compilation have **not** been verified. Check F10 pointer lock, keyboard+mouse capture, pad buttons and axes, profile reload, duplicate mapping conflicts, and focus loss on macOS and web. The UI intentionally does not modify project assets.

**Next agent:** review CI failures and run CMake/CTest, complete any remaining RTS camera/selection mappings and hosted-game input focus gating. Decide whether mobile touch gesture profiles need dedicated schema in a separate feature. Do not mark this PR ready without green CI.

## Stage 11 — explicit link wiring and agent skill refresh (2026-11-18)

**Changes:** root `CMakeLists.txt` now links `AIVERSE::lib_input` into both desktop `aiverse_client` and web/mobile `main` target, so `apps/client/ClientInputRouting.h` and `EnhancedInputConfig.h` resolve the public `<aiverselib/...>` include path in all build variants. Editor target already links the library. `skills/ui/aiverse-input/SKILL.md` now routes agents to the editor authoring UI, FPS player settings UI and RTS shortcut adapter; mandatory input validation now covers `library_enhanced_rts_input`.

**Verification:** GitHub connector source audit only. CI on last commit was queued at check time; compilation and CTest remain unverified. Do not mark these links compile-verified before CI/local build completes.

**Next agent:** check CI logs, run `aiverse_enhanced_input_tests`, adapter/config/RTS tests plus `aiverse_editor`, `aiverse_client`, `aiverse_fps_client`; test interactive rebind and hosted GameWindow/Scene focus. Fix compiler/warning failures immediately, append another handoff stage.

## Stage 12 — shared FPS/RTS presets and editor initialization (2026-11-18)

**Change:** `engine/platform/EnhancedInputPresets.h` now provides `enhanced_presets::fpsReference()` and `rtsReference()` producing complete version-1 `enhanced_config::Asset` instances with explicit stable IDs. FPS client `configureInput()` installs the exact same FPS preset; RTS `ClientInputRouting` installs the shared RTS preset. Editor Input Actions window adds **Use FPS defaults** and **Use RTS defaults** buttons, allowing agents/users to create complete mapping assets without forgetting default actions. Buttons replace the in-memory editor buffer only; Save persists them. Previous hardcoded FPS/RTS schemes were removed from the clients, preserving their semantic actions and order.

**Files:** `engine/platform/EnhancedInputPresets.h`, `apps/fps/client.cpp`, `apps/client/ClientInputRouting.h`, `apps/editor/windows/InputWindow.cpp`, `tests/libraries/enhanced_input_config_tests.cpp`, `libraries/catalog.json`, `docs/enhanced-input.md`, `skills/ui/aiverse-input/SKILL.md`, this handoff.

**Tests:** Added preset JSON write/read/install roundtrips and stable mapping ID assertions (ID 1..N) to `enhanced_input_config_tests.cpp`. Validation is not yet backed by a compiler or successful CTest; GitHub CI was queued. Run tests before promoting PR #20 out of draft.

**Next agent:** check CI errors, verify initialized FPS key controls parity and mouse look, verify project preset editing/saving plus runtime profiles and RTS UI focus, then run full cross-platform validation.

## Stage 13 — mobile touch movement axes and multi-touch safety (2026-11-18)

**Changes:** `EnhancedInputAdapter.h` now tracks each finger's previous position and emits accumulated per-frame X/Y motion through `Device::Touch` codes 1 and 2 (code 0 remains touch-down). Slot allocator reuses free indices 0..31; duplicate TouchBegin ignored; TouchEnd releases and can emit final delta, TouchCancel only releases, FocusLost clears everything. No device-specific backend change. The InputWindow numeric Touch code field can already author channels 0,1,2; JSON validates device indices.

**Test additions:** `enhanced_input_adapter_tests.cpp` now verifies two coalesced TouchMove events, transient movement completion, duplicate begin and slot recycling, TouchCancel release.

**Verification:** committed source/test changes; no running CTest or live iOS/Android verification. Need capture multi-touch motion on physical devices and hosted editor events, especially when UI owns touch.

**Next agent:** finish host/view focus arbitration and cross-platform build; run all tests, review modifiers/triggers for repeat-state parity, and record CI and manual checks in handoff before ending.

## Stage 14 — hosted editor Game window focus handoff (2026-11-18)

**Problem:** switching Game window to Inspector/Input Actions/editor text was not an OS FocusLost. The host stopped forwarding new keys, but previously held WASD and semantic actions stayed held and could move the game unexpectedly.

**Fix:** `apps/editor/ClientHost.h/.cpp::place` detects focus transitions and sends synthetic `FocusLost`/`FocusGained` to the hosted `GameClient` under its existing `Scope` (HostedView + ImGui state restored). Focus loss releases captured touch/pointer. Start/stop reset host focus. `apps/client/GameClient.cpp::event` also clears the old `keys_` array on FocusLost; semantic InputSystem::cancelAll is handled by `ClientInputRouting`.

**Build/testing:** code updates only, not yet compiled or manually verified; CI pending. Test Game→Inspector→Input Actions→Game with held WASD, RmlUi modal, touch dragging, and scoped context restoration. Network Command schemas unchanged.

**Next agent:** compile and run CI and manual hosted game tests, then document exact results.

## Stage 15 — upstream inspector sync and verification gate (2026-11-18)

`feature/universal-entity-inspector-fps` advanced one additional docs-only commit, `cf650a02d22890cbc3b27280762e8c0d96502bed`. The new `docs/editor-unification-handoff.md` was merged as-is with the Enhanced Input feature changes; no ECS/API conflict. PR #20 targets the inspector branch so reviewers see an input-focused diff, not the entire ECS history.

**Current validation gate:** the latest input-head GitHub workflows were `queued` for CI, AIVERSE Studio and ECS experimental at inspection time; older feature-head CI runs were canceled by newer commits. A complete CMake/CTest run was not available: the local isolated container cannot resolve github.com to clone AIVERSE, and the connected GitHub service only accesses source/CI metadata. Do **not** report green build, Apple Silicon integration success or all platforms verified. Keep PR draft until compilation/tests/manual smoke checks pass.

**Required local commands (from AIVERSE checkout):**

```sh
git fetch origin
git switch feature/aiv990_enhanced_input
git pull --ff-only origin feature/aiv990_enhanced_input
cmake -S tests/libraries -B build/enhanced-input-check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/enhanced-input-check --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests
ctest --test-dir build/enhanced-input-check --output-on-failure -R 'library_enhanced_(input|rts_input)'
python3 tools/libraries/catalog.py check
cmake --preset macos
cmake --build build/macos --config Release --target aiverse_editor aiverse_client aiverse_fps_client
```

**Smoke-test checklist:** editor opens Input Actions, selects FPS/RTS defaults and saves valid `input/actions.json`; FPS F10 rebinding persists across restart; gamepad/mouse/touch modifiers and profile IDs remain stable; Game→Inspector tab focus releases held RTS movement; RTS script-command overrides and network Commands unchanged. Run all AGENTS.md engine regression and determinism suites before merge. Cross-platform macOS/Linux/Windows/Web/Android/iOS remain unverified.

**Next agent entry point:** read this document and `docs/enhanced-input.md`; obtain build logs/CI results, fix warnings and failures, then write Stage 16 with exact test evidence. Avoid re-implementing the input core; remaining high-level gestures and non-hotkey RTS controls can follow as additive stages.

## Stage 16 — transactional user profile write/rebind (2026-11-18)

**Problem:** FPS `rebindInput` previously modified `actions` and `inputProfile` before saving; failed file writes could leave runtime keys changed without a matching durable profile. Existing Windows overwrite fallback also removed the original file before confirming replacement.

**Fix:** new `engine/platform/EnhancedInputProfileIO.h` provides `enhanced_config::loadProfileFile/saveProfileFile` with validated JSON, temporary writing, atomic POSIX replacement and backup/restore fallback for Windows. FPS stages both a copy of the current `InputSystem` and user `Profile`, validates/applyProfile into staging, persists the profile, then swaps the in-memory state and calls `syncUserData`. Invalid controls, unknown mapping IDs and disk errors leave the prior live mapping untouched. FPS initial profile load also uses staging, so invalid stored overrides are ignored without affecting defaults.

**Tests:** `enhanced_input_config_tests.cpp` adds host-file roundtrip plus failed invalid-profile overwrite retention using a temporary file. Runs on the existing `library_enhanced_input_config` target; test execution pending.

**Verification:** source changed and committed, CI jobs were still queued at inspection time, no local full checkout due blocked GitHub networking. Do not claim a passing build. Next agent: compile and run this test on Windows and POSIX, then check editor and RTS focus/profile parity before PR readiness.

## Stage 17 — two-finger pinch and centroid pan (2026-11-18)

**Code:** `EnhancedInputAdapter::updateGesture` recognizes exactly two active touches, sampling distance and centroid. `Device::Touch` control codes 3 (pinch distance delta), 4 (midpoint pan X), and 5 (midpoint pan Y) publish frame-accumulated relative values on `deviceIndex=0`. Re-baselines when a second finger joins, fingers leave/cancel or focus is lost, so no jump is emitted from contact changes. Per-finger 0/1/2 codes remain backward compatible. Add standard Action mappings to these codes in any project, including user-authored JSON.

**Tests:** `enhanced_input_adapter_tests.cpp` verifies two-finger activation without ghost delta, pinch +5, centroid pan -2.5, release completion, no jump on replacing a finger and focus-loss reset. Compile and CTest not yet verified.

**Limits:** gestures are input signals (not simulation commands), and UI must capture/fence touches first. Rotation gestures and multi-finger gestures other than exactly two are not implemented. The existing genre-specific RTS region gesture remains unchanged until it is migrated.

**Next agent:** verify builds and add a reference action-driven camera path; integrate RTS pointer/zoom and editor GameWindow where current behavior can be preserved, then update docs.
