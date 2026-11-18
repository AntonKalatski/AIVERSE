# AIVERSE Enhanced Input — agent handoff (living document)

Branch: `feature/aiv990_enhanced_input` (base `feature/universal-entity-inspector-fps`). Owner: input subsystem. Updated 2026-11-18.

**Mandatory workflow:** after EVERY stage update this file **in the same branch, before moving to another stage**. Record (1) numbered stage/date/goal/status, (2) architecture/API/invariants and exact file paths/commit, (3) the build/test commands and actual PASS/FAIL/NOT RUN evidence, (4) blockers, limitations and risks, and (5) a precise next-agent task with file/function entrypoints and acceptance criteria. No stage is considered complete without its handoff update. Never overwrite older stages or infer successful tests from queued CI. Keep `docs/enhanced-input.md` and `skills/ui/aiverse-input/SKILL.md` synchronized with any public changes. The user currently wants implementation first, testing later. See the binding rule in root `AGENTS.md`.

**Continuation protocol:** read this document's most recent stages, inspect the current Git branch head before writes, make a stage-scoped change, append the stage handoff and commit it, then begin the next stage. The next agent must be able to continue without asking the user to repeat architectural decisions.

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

## Stage 18 — RTS camera actions and lifecycle cleanup (2026-11-18)

**Implementation:** Extended `enhanced_presets::rtsReference()` with `CameraPan` (arrows) and `IdleCameraPan` (WASD when selection is empty), both `Axis2D` Actions in a new `RTS.Camera` context. Camera bindings are non-consuming so gameplay A/P/R/E/S/H actions remain available. Existing `RTS.Gameplay` mapping IDs 1..7 remain untouched for rebinding profile compatibility. `apps/client/GameClient.cpp::frame` now samples semantic camera axes rather than raw `keys_` for keyboard camera pan; gamepad and pointer-edge movement remain unchanged. `ClientUi.cpp::setScreen` clears semantic input when transitioning away from Playing to prevent held keys on pause/menu.

**Tests:** `enhanced_rts_input_tests.cpp` now asserts arrows/idle camera analog values, action coexistence and dual contexts. No on-device/CI verification yet.

**Behavior:** Axis mapping is combined with original gamepad and edge scroll, clamped per direction to preserve normal speed (identical arrow+W no longer doubles speed, intentional). Pointer selection, project scripts and top-down possession still have their specialized input, preserving game rules and commands. Project `input/actions.json` is a complete replacement and must define camera bindings explicitly if desired.

**Next agent:** migrate compatible editor GameWindow camera controls, verify old RTS movement/selection and keyboard focus, run CMake+CTest for library RTS input and native clients, then log results.

## Stage 19 — editor GameWindow camera routing (2026-11-18)

**Implementation:** `apps/editor/windows/GameWindow.cpp` now installs the shared RTS semantic camera mapping (or the project's `input/actions.json`) into a presentation-only `InputSystem`. It polls only the declared keyboard controls through ImGui when the GameWindow is focused and no text field owns input; unfocused/text capture calls `cancelAll()`. Camera movement uses the resulting `CameraPan` + `IdleCameraPan` values instead of direct WASD/arrow conditionals. The project asset is rechecked at most twice per second while the GameWindow is drawn, so edited bindings become active without restarting. Direct FPS projects and hosted GameClient continue their separate input paths.

**Compatibility:** The editor's direct RTS play historically allows WASD camera pan regardless of selected units; unlike the standalone RTS client, both Axis2D actions are combined unconditionally. Pointer/selection gestures, regions and GameHud retain their specialized handling. No authoritative ECS state, gameplay Commands or serialized command layout changed.

**Verification:** GitHub source changes only, no full editor compile/interactive test yet. A focused validation must check ImGui key enum mappings, window focus cancellation, readAsset load/reload and project input hot-rebind. Test selection + keyboard camera, script HUD popup, region camera and initial missing input file fallback.

**Next agent:** check CI and compiler outputs; fix any editor/RTS input warnings and verify functional parity. Do not mark PR ready without green validation. Continue documenting new stages.

## Stage 20 — validated editor project asset writes (2026-11-18)

**Problem:** `InputWindow::save` previously removed an existing `input/actions.json` before renaming the temporary file. On Windows or filesystem errors this could lose user-authored mappings.

**Fix:** New `engine/platform/EnhancedInputFileIO.h` provides schema-validated `loadAssetFile/saveAssetFile` and the shared `detail::saveValidatedJsonFile` writer (atomic POSIX replace or Windows backup/restore fallback). `EnhancedInputProfileIO.h` reuses the same writer. `apps/editor/windows/InputWindow.cpp` now loads/saves via the checked library instead of handwritten destructive file operations. The UI still rejects empty Actions/Contexts and retains its last in-memory buffer on errors.

**Test coverage added:** `enhanced_input_config_tests.cpp` now creates a project action asset in a temporary file, validates roundtrip, attempts invalid replacement and verifies the previous data stays intact. The pre-existing user-profile roundtrip/failure tests remain.

**Source/test status:** GitHub source committed; CMake/CTest and macOS/Windows GUI verification remain pending. Need check user asset read-only errors and project-switch/reload behavior on actual OS. Both file APIs are presentation-only; no sim/ECS format changed.

**Next agent:** inspect CI and build; debug errors; run UI focus/rebind/camera functional test and full AGENTS.md validation before making PR ready.

## Stage 21 — RTS semantic pointer controls / wheel zoom (2026-11-18)

**Implementation:** `enhanced_presets::rtsReference()` adds `RTS.Pointer` Mapping Context with `Select`, `Command` Boolean press Actions and `CameraZoom` Axis1D on mouse wheel Y, all non-consuming and assigned stable mapping IDs 1..3. `ClientInputRouting` owns `platform::EnhancedInputAdapter` and forwards keyboard/pointer/touch events through the shared runtime **after UI pointer ownership is resolved**. Its `reset` handles Touch/held input through the adapter. `GameClient::event` now derives ordinary camera wheel zoom from semantic `CameraZoom` value instead of raw `e.wheelY`. Pointer selection/drag/garrison/tap semantics still execute their existing high-level logic, but projects and agents can bind to `Select`/`Command` Actions without duplicating low-level device conversion. RTS native command format unchanged.

**Tests:** `enhanced_rts_input_tests.cpp` verifies CameraZoom wheel value and UI blocking plus Select click; registered context count now 3. Real editor/gamepad/multitouch behavioral validation still pending.

**Next agent:** assess code/CI diagnostics, compile and test the complete suite, and document any remaining native/Windows/Web issues. Do not replace RTS selection algorithms with simple button clicks: selection is a distinct high-level mechanic and stays presentation-side.

## Stage 22 — hidden hosted Game tab input isolation (2026-11-18)

**Problem:** `Editor::event` forwarded every native input event to `ClientHost`. A hidden Game dock tab might stop drawing before `ClientHost::place(...,focused=false)` ran, so stale `focused_` or `captured_` could continue forwarding keyboard and pointer input while the user edits an Input Action/Inspector field.

**Fix:** `apps/editor/ClientHost.h/.cpp` adds `suspendInput()` to send hosted `FocusLost` and clear keyboard/pointer/touch capture without requiring a draw. `Editor::event` checks that the Game tab is open and in front; a hidden client is suspended and receives no further ordinary events. Real OS `FocusLost` remains forwarded to clean up the host. This is in addition to Stage 14's synthetic focus edges in `ClientHost::place` for ordinary focus changes while Game is drawn.

**Files:** `apps/editor/ClientHost.h/.cpp`, `apps/editor/Editor.cpp`, `docs/enhanced-input.md`, this handoff.

**Verification:** GitHub source committed; no macOS GUI runtime test or CTest result. Verify two dock tabs (Game/Input Actions) while holding WASD, dragging from an embedded game screen, text entry, switching back/forth and global FocusLost while hidden. Input must not leak, duplicate clicks or remain stuck.

**Next agent:** check all CI jobs, build editor, client and FPS; run interaction smoke tests, document exact verified evidence, and then consider merge approval.

## Stage 23 — refresh inspector parent and release validation snapshot (2026-11-18)

`feature/universal-entity-inspector-fps` advanced from `cf650a02d22890cbc3b27280762e8c0d96502bed` to `4a1173ce26a7f09c7fe0438755d28f55a7a1233b` (two docs-only commits). Its updated `docs/editor-unification-handoff.md` and new `docs/agent-handoffs/f90242ec4-integration.md` were carried forward in a two-parent merge with the current input feature branch. No overlapping C++ or test changes were present. Repeat the comparison just before PR merge because the inspector branch may still move.

**Validation snapshot:** all four targeted CTest executables are registered, and GitHub source was audited for link paths/brace balance; CI had not yet returned green results when inspected. Previous input branch workflow runs were often canceled by subsequent commits, and later runs remained queued. A full macOS/Windows/Linux/Web/Android/iOS build, editor runtime smoke test and complete engine determinism suite have NOT been completed in this environment. The GitHub-enabled connector can update code but cannot execute a compiler; the working container has no GitHub network route for a complete clone. Never claim tests passed or mark PR ready based on only source inspection.

**Next agent:** before merging PR #20, run the exact commands in Stage 15, inspect CI job logs, verify project asset writes (invalid JSON, read-only paths), FPS F10 rebinding across restart, RTS camera/zoom, Game vs Input Actions dock focus isolation and two-finger gestures on actual devices. Append verified build/test evidence as Stage 24. Keep the PR draft until then.

## Stage 24 — Action-level modifiers gate lifecycle (2026-11-18)

**Bug:** `InputSystem::update` previously marked an Action active and `Triggered` based on pre-Action aggregate input. `Action::modifiers` (such as DeadZone or Scale(0)) and `ValueType` constraints could then produce a zero Action Value while still emitting Started/Ongoing/Triggered. This violates the typed-action pipeline.

**Fix:** `EnhancedInput.h` computes the post-modifier constrained Action Value before updating action lifetime state. `ActionState.active`, Started/Ongoing/Completed and default trigger eligibility follow that final value. Release/one-frame Tap pulses still work even when the released value is zero. Mapping-level trigger timing and network command formats are unchanged.

**Regression:** `enhanced_input_tests.cpp` now covers a sub-threshold Axis1D Action-level dead zone, a Scale(0) disabled Action, a threshold crossing that starts action, and crossing back below threshold that completes it.

**Validation:** source and tests committed, no execution results from full project CI yet. Run `library_enhanced_input` and FPS input+gamepad integration before merging. Next agent should check composite chord and same-frame tap regressions, record actual compiler/test results in Stage 25.

## Stage 25 — independent cross-platform Enhanced Input CI gate (2026-11-18)

**Change:** new `.github/workflows/enhanced-input.yml` runs a focused C++20 library regression matrix on `ubuntu-24.04`, `macos-15`, `windows-2022`. It configures the existing standalone `tests/libraries/CMakeLists.txt`, builds four targets (`aiverse_enhanced_input_tests`, `aiverse_enhanced_input_adapter_tests`, `aiverse_enhanced_input_config_tests`, `aiverse_enhanced_rts_input_tests`) with warning-as-error settings already set by CMake, and runs their CTest names. Push, PR changes in input-related files and explicit manual workflow dispatch can trigger it.

**Why:** the monolithic engine CI was still queued after multiple incremental commits. The focused job has a much smaller compile surface than complete editor/FPS packages, but does not replace full platform CI and macOS visual play-test.

**Validation:** workflow committed; GitHub Actions results not yet completed at writing. Watch the matrix for compiler warnings, link errors, test assertions and Windows filesystem backup behavior. The current feature remains a draft PR, pending green results.

**Next agent:** inspect `Actions → Enhanced Input targeted checks` and fix failures; run full editor/client/FPS builds, engine regressions and manual input/hosted viewport smoke tests. Add Stage 26 with exact per-platform PASS/FAIL evidence before marking PR ready.

## Stage 26 — deterministic binding conflicts and rebind policies (2026-11-18)

**Core:** `lib::gameplay::enhanced::RebindPolicy` is `Allow` (backwards-compatible default), `Reject` (fail if another mapping in the same context already uses the target control), or `Swap` (exchange controls with exactly one conflicting mapping, preserving stable IDs; reject multi-way ambiguous collisions). `InputSystem::conflicts(activeOnly=true)` enumerates all same-control collisions in actual context dispatch precedence, identifies the two context/actions/mapping IDs, and flags when the earlier consumer shadows the later mapping. It ignores inactive contexts unless asked explicitly.

**Tests added (not run):** `tests/libraries/enhanced_input_tests.cpp` exercises Reject/Swap/Allow, ID preservation and priority/shadowing report. Existing `rebind(context,id,control)` callers retain their previous Allow semantics.

**Why:** two Actions sharing the same physical button is often intentional (e.g., tap vs hold or gameplay versus modal UI). Automatically deleting duplicates would silently corrupt authored input schemes. Agents should query `conflicts()` and offer users a choice, never mutate mappings without an explicit policy.

**Files:** `EnhancedInput.h`, unit test, `docs/enhanced-input.md`, this handoff. **Validation:** postponed at user request; no compiler or runtime evidence yet. Next stage: expose conflict resolution in Input Actions editor/FPS Settings and document.

## Stage 27 — advanced Unreal-style triggers (2026-11-18)

New cross-genre Trigger types: `DoubleTap(seconds)` (second press after a preceding release within a bounded interval); `HoldAndRelease(seconds)` (release after sufficient hold time); `Pulse(seconds)` (periodic trigger while held, min 10 ms); `NotChord(action)` (inverted chord blocker; required action must be inactive). Core maintains per-mapping runtime release window/pulse timing and continues to accumulate same-frame inputs. JSON V1 recognizes `doubleTap`, `holdAndRelease`, `pulse`, `notChord`; encoder writes matching values. Editor Input Actions trigger dropdown/seconds/chord selection supports all ten types. Chord validation also checks the inverted blocker.

**Files:** EnhancedInput.h, EnhancedInputConfig.h, InputWindow.cpp, enhanced_input_tests.cpp, this handoff and docs. Tests authored for double-tap, hold release, pulse, inverted chord; execution intentionally deferred at user's request.

**Limitations / next:** the new types are additive, preserving all existing enum ordinal values and JSON syntax. Advanced timing is per active mapping/context, not saved in the deterministic sim. Review repeated activations after context changes and cyclic complex chord definitions during later validation.

## Stage 28 — multi-local-user input ownership (2026-11-18)

**New reusable API:** `engine/platform/EnhancedInputUsers.h`, `platform::EnhancedInputUsers`, owns independent Enhanced Input core/adapter per local `UserId` (not an ECS entity). Add user with `addUser(id, InputSystem scheme)`; assign keyboard/pointer/touch individually with `setKeyboardOwner`, `setPointerOwner`, `setTouchOwner`, and physical gamepad with `assignGamepad(pad,id)`. `onEvent(event, gameplayAllowed)` forwards only to the rightful user after UI capture; `pollGamepads()` polls hardware **once**, samples each assigned physical pad into the user's logical `deviceIndex=0`, and `update(id,dt)` returns only that user's Action events. Ownership transfers, unassignment, UI capture and OS FocusLost cancel held Actions; one pad per user is enforced. A project may create multiple simultaneous local players (split-screen, couch co-op); no shared static keyboard state or genre restriction.

**Compatibility:** Existing `EnhancedInputAdapter`/single-user FPS/RTS APIs are unchanged. New router is optional; avoid installing it in a single-user game if there is already a platform adapter polling the same hardware. For local-player gamepads, author logical gamepad index **0** per player; this is intentionally independent of physical pad IDs. `EnhancedInputUsers` lives in platform presentation code, not deterministic ECS.

**Tests added (deferred):** `enhanced_input_adapter_tests.cpp` checks isolated keyboard vs pad events, assignment validation, canceled pad state on detach and focus cleanup. `libraries/catalog.json` and the input AI skill include the new reusable native module.

**Next:** connect to optional split-screen sample/preset and expose player-specific profile storage; then continue top-down client migration. Runtime validation and CTest explicitly deferred by user.

## Stage 29 — top-down combat/movement migration (2026-11-18)

**New preset context:** `RTS.TopDown` (priority 100, inactive until a top-down hero is controlled) supplies semantic Actions `TopDownMove` Axis2D (WASD/arrows), `TopDownFire` (Space/right mouse), `TopDownAim` (left mouse), `TopDownEnter` (F/Enter), `TopDownWeaponNext` (X/Tab), `TopDownWeaponPrev` (Z), and `TopDownWeapon1..9` (number keys). Existing RTS.Gameplay/RTS.Camera/RTS.Pointer mapping IDs are unchanged. `ClientInputRouting::setTopDownEnabled` activates/deactivates this context without mixing genres into the core library.

**Client:** `GameClient::event` updates that context before normal event routing. `GameClient::topDownEvent` now consumes semantic one-shot Actions for weapon cycling, weapon slots and entering vehicles (legacy key fallback if a project-supplied scheme lacks RTS.TopDown). `topDownFrame` samples semantic `TopDownMove`, `TopDownFire`, `TopDownAim` from existing input state, preserving keyboard fallback for older action schemas. It still sends existing `kCmdTopDown` / arsenal Commands and continues using touch-stick/aim assistance/prediction calculations and project-specific ability buttons from `TopDownControls`.

**Scope limits:** project-authored `input/actions.json` is a full replacement: rebind named TopDown Actions there to customize the hero. Dynamic game-specific ability button keys and commerce script keys still use their explicit game.json/script mapping rules; these are not interchangeable generic camera/weapon actions. Keyboard is captured after UI filtering. Run top-down regression after user-requested implementation phase.

**Next:** expose conflicts and policies in editor FPS settings, improve per-player profiles, map RTS touch gesture deltas through Actions, then perform static review and defer actual CTest/CI until requested.

## Stage 30 — conflict policies usable in editor and FPS settings (2026-11-18)

**Editor:** `InputWindow` now has `On capture conflict` (Allow duplicate / Reject duplicate / Swap keys). Keyboard-capture uses that policy, preserving IDs and swapping exactly one previous binding when requested; ambiguous multi-way collisions give an explicit error. It also shows `InputSystem::conflicts()` across active Mapping Contexts with higher/lower contexts, Action names, stable mapping IDs, and shadowing information.

**FPS:** F10 `Input Settings` has the same three conflict policies, default Swap, plus a cross-context conflict viewer. `rebindInput(...,RebindPolicy)` stages a copy of the whole input system. In Swap mode it records **both affected mapping IDs** into a staged validated user profile, verifies the profile by applying it to a copy of the current system, persists to disk, then commits the runtime state. A failed Reject/ambiguous Swap/validation/disk save leaves both live input and profile unchanged and reports an explicit error.

**Files:** `apps/editor/windows/InputWindow.cpp`, `apps/fps/client.cpp`, this handoff and user guide.

**Unverified:** code committed; actual compilation, F10 UI, and profile restart/Swap smoke tests intentionally deferred until user testing phase. Need verify ImGui combo labels, capture and syncUserData on real macOS/Windows.

**Next:** per-user player profiles and optional Action-based RTS touch camera gestures, then source-level closure audit. No tests run yet.

## Stage 31 — shared per-user profile store, FPS migration and authored preset test fix (2026-11-18)

**New API:** `engine/platform/EnhancedInputUserProfiles.h` (`enhanced_config::UserProfiles`) provides `setScope(userDataDir,projectIdentifier)`, `path(userId)`, `load(userId,InputSystem)`, `rebind(userId,InputSystem,context,id,control,policy)`, and `reset(userId,InputSystem,projectDefaults)`. Profiles are isolated by project and numeric local-user ID. File hashing is stable FNV-1a; user 0 retains the existing FPS `enhanced-input-HASH.json` name for backwards compatibility; users 1+ use `-user-<id>`. All writes validate first, preserve previous state on failure and persist **both IDs** for Swap.

**FPS integration:** `apps/fps/client.cpp` now uses `UserProfiles` for load/rebind instead of duplicating per-profile storage logic; project-authored defaults are cached before applying player 0 overrides. F10 UI includes `Restore project defaults`, which resets the user's profile without changing `input/actions.json`, and uses the shared transactional persistence. Still a single network/local player in FPS sample; the multi-user service is available for native split-screen projects.

**Tests authored and obvious stale assumptions fixed (execution deferred):** `enhanced_input_config_tests.cpp` no longer assumes RTS has exactly one context (it grew Gameplay/Camera/Pointer/TopDown); checks all contexts' stable IDs. New profile tests cover two isolated player paths, swapping two FPS keys, restart/restore and reset to defaults. `enhanced_rts_input_tests.cpp` expects four default RTS contexts. Catalog and agent skill updated.

**Next:** close remaining RTS touch/gesture routing and player UI input focus; inspect source/CI only, no tests until user requests test phase.

## Stage 32 — RTS single- and two-finger gestures via Actions (2026-11-18)

**New default context:** `RTS.Gestures` (active/non-consuming) defines `CameraTouchDrag` Axis2D (per-finger slot 0 delta codes 1,2), `CameraTouchPan` Axis2D (two-finger centroid codes 4,5), and `CameraPinch` Axis1D (two-finger distance code 3). Existing `EnhancedInputAdapter` has produced these channels since Stage 17, and RTS now maps them explicitly like other input assets.

**RTS client:** `GameClient::event` keeps world-ground projection, touch selection/drag logic and camera distance clamping, but uses the Action values for single-finger pan, two-finger centroid pan and relative pinch zoom. `CameraPinch` modifiers can tune sensitivity without changing raw contact baseline. If a custom project's full `input/actions.json` lacks these actions, it falls back to legacy world-space touch deltas for compatibility.

**Tests authored (deferred):** `enhanced_rts_input_tests.cpp` expects five default RTS contexts and verifies TopDown context toggling, `RTS.Gestures` active. Existing platform adapter test covers raw two-finger motion. No CI/CTest run requested now.

**Next:** source audit, remove unnecessary duplicate legacy state where safe, document what project-owned touch HUD/Script abilities intentionally remain specialized. Do not move gesture projection or network Commands into the platform input library.

## Stage 33 — action-aware RTS pointer selection and commands (2026-11-18)

**Problem:** although RTS.Pointer had `Select`/`Command` Actions, the world input handler still used physical `MouseButton(0/1)` to start drag-selection or smart orders. An authored rebinding could update InputSystem without changing gameplay.

**Fix:** `ClientInputRouting::logicalMouseButton(physical)` resolves a native button through the currently active `RTS.Pointer` context, treating Select as logical 0 and Command as logical 1. Unassigned buttons return -1, middle mouse remains logical 2 for panning by default; ambiguous duplicate physical bindings are visible via `InputSystem::conflicts` and Select takes precedence. `GameClient::event` preserves the original native Event for UI/platform routing and before passing world clicks to script commands, tools, selection and smart orders rewrites only the logical button in a local Event copy. This makes RTS selection/command mouse-button rebinding functional **without** duplicating the geometry of click/box/garrison interactions, and bypasses it for top-down, planet and region modes.

**Tests authored (deferred):** RTS bridge test checks logical 0/1/2 defaults and rebinding Select to mouse-button 3 while suppressing unbound physical button 0.

**Next:** revisit top-down dynamic game.json abilities and input focus UI ownership, then cross-file source review. No tests run as requested.

## Stage 34 — top-down project-configured ability keys through Actions (2026-11-18)

**Problem:** top-down movement/weapons used `RTS.TopDown` Actions but custom `game.json.topDown.buttons[*].key` still bypassed Enhanced Input, so generated game-specific ability buttons could not be remapped through the shared API.

**Fix:** when a top-down hero is configured, `GameClient::topDownConfigure` synthesizes `TopDownAbility0..N` Boolean Actions and `RTS.TopDownAbilities` Mapping Context from the game's configured button keys if the project hasn't authored that context already. The context uses Pressed triggers and stable binding IDs. Project-authored `RTS.TopDownAbilities` can override this, preserving full user control. `ClientInputRouting::setTopDownEnabled` toggles the optional ability context together with `RTS.TopDown`. `topDownEvent` now dispatches buttonDown based on semantic Triggered Action names; per-key held state releases the corresponding button when its Action becomes inactive, even if the author changed its physical key. On focus loss all held ability flags and virtual controls are canceled.

**Compatibility:** touch HUD pointer buttons and the ability execution rules remain in `TopDownControls`/native Commands, not generic input. Game commerce scripts and vehicle authority remain specific mechanics. The generated context is runtime-only; for authoring custom controls, include `RTS.TopDownAbilities` and `TopDownAbilityN` Actions in the project's full input asset.

**Source audit fix:** corrected the `in::Device` typo in `enhanced_rts_input_tests.cpp` to the declared `ei::Device` namespace. Tests are authored but not executed per user request.

**Next:** finish source-level review, ensure no legacy direct key leaks, and leave runtime/CI/CTest to the subsequent validation phase.

## Stage 35 — frame-advancing RTS and top-down time triggers (2026-11-18)

**Bug:** `ClientInputRouting::onEvent` advanced the generic InputSystem with `dt=0` only on OS messages. Timed `Hold`, `Pulse`, `HoldAndRelease`, and continuous analog state would not advance in RTS during a stationary held key, even though FPS sampled input every frame.

**Fix:** `ClientInputRouting::advance(dt)` advances Action lifetimes once each presentation frame and returns semantic Triggered names. `GameClient::frame` invokes it after the existing gamepad injection step while playing and without a text field/modal how-to; otherwise it cancels held state. `GameClient::applyRtsActions` centralizes high-level RTS orders/FocusCamera for both physical event and timer-triggered events, producing the same existing Commands. Custom script-command hotkeys still win: `ClientInputRouting::suppressKey` releases the physical Action control on script override so delayed Hold/Pulse cannot later steal it. `topDownTimedActions` dispatches timed custom ability Actions into the existing module button state; `topDownFrame` releases held abilities when their Action becomes inactive, including canceled contexts.

**Files:** `apps/client/ClientInputRouting.h`, `GameClient.cpp/.h`, `ClientTopDown.cpp`, docs and this handoff. No changes to deterministic sim / ECS command formats.

**Validation deferred:** tests and gameplay smoke pass after implementation phase, per user. Verify script override, UI text capture, 0-dt event press, frame hold trigger, topdown button release and FPS parity later.

**Next:** close outstanding low-level editor input and cross-file audit; avoid treating per-event Action update as a substitute for per-frame updates.

## Stage 36 — editor direct GameWindow pointer binding parity (2026-11-18)

**Fix:** `apps/editor/windows/GameWindow.cpp` direct RTS play-test now resolves physical mouse buttons via the same authored `RTS.Pointer` context as the native RTS client, with shared semantics `Select` and `Command`. A focused helper `boundButton(action,legacyFallback)` finds the configured mouse button (or returns ImGuiMouseButton_COUNT when explicitly unbound); `actionMouseClicked/Down/Released` are used by selection boxes, building placement/cancel, script-card picking, region operations, and mouse-owned entity/action gestures. `refreshCameraActions(ed)` now runs **before** script-card click processing each frame so live project edits apply immediately to all world pointer paths. Camera middle-drag does not steal input if Select or Command has been explicitly rebound to middle.

**Compatibility:** pointers still go through ImGui/HUD capture and spatial picking in the editor; this is a presentation-layer mapping only, never deterministic ECS state. If a project's older full input asset has no `RTS.Pointer`, legacy left/right buttons remain the fallback. Custom bindings to non-mouse devices are not synthesized into fake editor mouse clicks; those need a project-defined UI interaction rather than reinterpreting cursor coordinates.

**Validation deferred:** source committed. During manual validation, test selected Game dock, script cards and viewport, rebind Select to button 3, drag boxes, Command to button 4, and middle-button camera pan collision. Recheck editor GameWindow input in hosted-client mode (separate event forwarding).

**Next:** release-stage static source audit, docs status, CI later; do not mark PR green without build evidence.

## Stage 37 — normalized two-finger twist input (2026-11-18)

**Adapter:** `EnhancedInputAdapter::updateGesture` now publishes `Device::Touch`, code 6, deviceIndex 0 for the shortest-arc two-finger rotation delta in radians (signed, wrapped to -π..π). It uses the same gesture activation/re-baseline guard as pinch/pan, so adding/removing a finger does not jump orientation. Camera consumers must integrate it per frame; the platform remains presentation-only.

**Preset:** `RTS.Gestures` adds `CameraTwist` Axis1D bound to Touch code 6, non-consuming. Existing codes 0..5 and mapping IDs are unchanged; only a new mapping is appended, so prior per-user override IDs remain stable.

**Test authored (deferred):** adapter test checks a deliberate rotated two-finger segment produces nonzero twist and does not lose pinch handling. No execution as requested.

**Next:** use normalized `CameraTwist` with planet orbit camera, and wire planet/possession movement to semantic Actions. Keep scripting and world projection genre-specific.

## Stage 38 — planet orbit and possessed-unit Action contexts (2026-11-18)

**Presets:** `RTS.Planet` (inactive except planet view) defines `PlanetRotate` Axis1D (Q/E), `PlanetPitch` Axis1D (PageUp/Down), `PlanetNextBody` Pressed (Tab). `RTS.Possession` (inactive except controlled unit) defines `PossessedMove` Axis2D (WASD/arrows), `PossessedFire` (left mouse) and `PossessedWalk` (right mouse). All previously published context IDs remain stable; default RTS now has **seven** contexts. `ClientInputRouting::setPlanetEnabled`/`setPossessionEnabled` maintain active contexts by game mode from both `GameClient::event` and `frame`.

**Possession:** `ClientActions.cpp::steerPossessed` computes camera-relative motion from semantic axis when available, otherwise legacy keys. Mouse fire/walk holds use semantic Action state with legacy fallback. The pointer logical LMB/RMB remapping path excludes possession to avoid stealing its gameplay controls.

**Planet:** `ClientPlanet.cpp::planetCameraKeys` uses shared `CameraPan`/ `IdleCameraPan` plus new `PlanetRotate/Pitch` and retains edge scroll/selection-dependent gating. `planetCameraEvent` takes semantic Triggered action names so rebinding Tab to `PlanetNextBody` and Space to FocusCamera works without repeat on unrelated keydown. Wheel zoom and one/two-finger pan/pinch/twist now consume `CameraZoom`, `CameraTouchDrag`, `CameraTouchPan`, `CameraPinch`, `CameraTwist` state, with legacy fallback for complete custom project assets lacking semantic actions.

**Tests authored (execution postponed):** RTS routing expectations updated to seven contexts; verify planet/possession activation and Axis2D motion. Follow-up smoke list: Q/E with selections, orbit body cycle, remapped wheel pinch/twist, possession fire/steer, reset on leaving view.

**Next:** audit contextual activation and C++ headers; finish docs/agent handoff. Then code is feature-complete for the stated scope, pending dedicated build/test/bugfix pass.

## Stage 39 — code-only integration audit / top-down project switches (2026-11-18)

**Source correction:** `GameClient` now tracks whether `RTS.TopDownAbilities` was synthesized by the engine. A later `topDownConfigure` re-registers an engine-generated ability mapping when the map/hero changes, preventing stale ability keys/indices from a previous level. A full authored `RTS.TopDownAbilities` context is never overwritten. Existing preset IDs remain unchanged.

**Editor correction:** `GameWindow` no longer refreshes project input twice per frame (once before script-card click processing, again in its input path); the earlier load remains, which is necessary so all direct-play pointer/camera actions use the same current authored scheme.

**Static audit (no test execution):** inspected source files for native core, adapter, JSON/profile file I/O, per-local-user routing/profiles, FPS, RTS, top-down, planet, editor, and CTest. Basic bracket/quote balancing across 21 files found no unmatched delimiters, and declared top-down/planet method signatures match their call sites by text inspection. This is NOT a compiler, linker, unit-test or runtime validation; platform-specific type correctness and behavior remain unverified at user's request.

**Post-implementation validation checklist:** (1) configure/build editor, RTS and FPS targets on macOS Apple Silicon, Linux, Windows; (2) CTest `library_enhanced_input`, `library_enhanced_input_adapter`, `library_enhanced_input_config`, `library_enhanced_rts_input`; (3) full ECS/determinism and FPS/RTS regression tests; (4) FPS F10 capture+Swap+restart+reset and project JSON reload; (5) editor dock focus/tab switching, project/Input window and direct GameWindow mouse selection; (6) keyboard/gamepad, drag, gestures and top-down/planet/possession mode transitions; (7) local multiplayer owner swap and per-player profiles; (8) Android/iOS/Web input smoke tests. Fix test/build failures in this same feature branch and append Stage 40 logs before making PR ready.

**Feature status:** implementation delivered at source level, tests intentionally deferred. PR #20 stays Draft; no claim of green CI or working binary.

## Stage 40 — synchronize latest inspector and handoff implementation to test phase (2026-11-18)

**Upstream:** merged `feature/universal-entity-inspector-fps` commit `309d7035e7317c5e3be66e8118298d2b64d490a1` (docs-only unified test runner + editor-unification handoff changes) as an actual two-parent merge into the input feature branch. No code or tests were overwritten, and input branch now contains its inspector parent history.

**Code implementation handoff:** the requested Enhanced Input functionality is committed: typed Actions and contexts, Modifiers/Triggers including complex timing, priorities/consumption, optional same-control conflicts and rebind policies, keyboard/mouse/touch (pan/pinch/twist)/pad, per-local-player device routing, per-user validated/transactional profiles and reset, FPS F10 Settings and project schemes, RTS keyboard and command bindings, top-down movement/weapons/abilities, possessed-unit actions, planet camera and gestures, editor Input Actions authoring and direct/hosted viewport capture. All code changes are presentation/native only; game rules continue using existing ECS systems and validated Commands.

**Test-phase entry gate:** the user explicitly asked to **finish code first and run tests afterward**. Therefore no compile, link, CTest, native GUI, device smoke, CI-success assertion, merge approval or release is claimed. PR #20 remains a DRAFT until build/test failures are resolved and evidence is recorded as Stage 41.

**Commands for the next agent:** run from repository root `cmake -S tests/libraries -B build/enhanced-input -DCMAKE_BUILD_TYPE=Debug`, then `cmake --build build/enhanced-input --parallel 2 --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests`, then `ctest --test-dir build/enhanced-input --output-on-failure -R "^library_enhanced_(input|rts_input)"`. Run full `./run-tests.sh` per AGENTS.md and platform/editor client smoke checks. Use the existing focused GitHub workflow for Linux/macOS/Windows, then review full CI. Amend code/hand-off for each failure; only then mark PR ready.

**Watch-outs for testing:** static C++ inspection cannot validate touch reset timing, cross-platform path rename, ImGui version, simulated gamepad behavior, button overlap with custom scripts or split-screen transitions. The new multi-user API is a reusable integration building block, not a claim that the sample FPS game has complete split-screen rendering/network identity.

## Stage 41 — C++ NotChord source-scope fix (2026-11-18)

**Goal/status:** IMPLEMENTED source-level correction; COMPILATION/TESTS NOT RUN at user's request (implementation first).

**Defect:** `InputSystem::chordSatisfied` was missing braces around its `for (const auto& trigger : triggers)` loop. `NotChord` subsequently used `trigger` outside its C++ scope, causing a compile error. The previous source-level bracket-count audit did not catch this semantic scoping error.

**Changed files:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` (wrap both Chord and NotChord checks in the same loop); `docs/enhanced-input-agent-handoff.md` (this entry); `docs/enhanced-input.md` (correctness note). Public API and JSON format unchanged. Behavior intended: every Chord/NotChord constraint in the trigger span is evaluated against the same-frame action-level map; the first unsatisfied constraint rejects the mapping.

**Verification:** static source inspection only. `library_enhanced_input` and the full CI have NOT RUN / no passing run evidence. The existing `tests/libraries/enhanced_input_tests.cpp` advanced trigger scenarios already include `Trigger::notChord`; rerun those when testing is authorized.

**Known risks:** composite trigger edge cases and time advancement still warrant deeper audit. A separate thread is pushing commits to `feature/universal-entity-inspector-fps`; compare and merge upstream before final PR review.

**Next agent task:** inspect `EnhancedInput.h::update/evaluateTriggers` for trigger lifetime correctness, especially same-frame press/release, DoubleTap and gated chords. Update regression cases, then append Stage 42 before advancing. Do not claim compilation success or merge PR #20 yet.

## Stage 42 — context-priority and modifier-aware chord preview (2026-11-18)

**Goal/status:** IMPLEMENTED source correction. Build/tests deliberately **NOT RUN** (user asked to finish implementation before testing).

**Defect:** `InputSystem::update` previously built same-frame `rawLevels` for Chord/NotChord from *all* physically actuated controls without checking active-context consumption, action-level modifiers or ValueType constraints. This could incorrectly fire `AltFire` on a gameplay Aim button already consumed by a higher-priority Menu context, or on Aim suppressed by an Action DeadZone.

**Change / API:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` now previews candidate action values in normal priority/name/registration order, honors consuming mappings, applies mapping and action modifiers and the target ValueType, and only then produces the chord-presence map. The preview is a read-only level calculation: it does not touch runtime timers/phases or emit actions. Public API and stored JSON schema are unchanged.

**Tests authored, not executed:** `tests/libraries/enhanced_input_tests.cpp` now asserts that a Menu context consuming Aim prevents a same-frame gameplay chord, and that Aim below an Action-level DeadZone threshold doesn't activate AltFire while Aim above the threshold does. Run `library_enhanced_input` during the later validation pass.

**Known limitation:** the preview establishes *physical/action-value presence*, not a full recursively evaluated trigger dependency graph. Chained Chord/NotChord gates and cycles still require an explicit resolution policy; do not describe this as recursive Unreal dependency parity.

**Next agent task:** inspect `EnhancedInput.h::chordSatisfied/evaluateTriggers`, `EnhancedInputConfig.h::checkChords`, and `tests/libraries/enhanced_input_tests.cpp`. Define deterministic handling of cyclic and chained chord dependencies without evaluation-order ambiguity, update API/config/editor docs and tests, and write Stage 43 before proceeding. Keep legacy `Input.h` and simulation Commands unchanged.

## Stage 43 — introduce AIVerse InputFlow as the official subsystem name (2026-11-18)

**Goal/status:** IMPLEMENTED the canonical public C++ name in GitHub; compile/CTest and platform validation **NOT RUN** (user requested implementation before testing). Product name: **AIVerse InputFlow** (short: **InputFlow**), emphasizing the pipeline of Device → Mapping Context → Action → Trigger → gameplay Commands, independently of game genre.

**Changed files/API:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` renames the C++ implementation class from `InputSystem` to `InputFlow` and adds `using InputSystem = InputFlow` in the legacy `aiverse::lib::gameplay::enhanced` namespace. New canonical public header `libraries/include/aiverselib/gameplay/InputFlow.h` exports `aiverse::lib::gameplay::inputflow::InputFlow`, associated Actions, Contexts, Values, Modifiers, Triggers, Controls and RebindPolicy; offers the friendly aliases `InputAction`, `InputContext`, `InputValue`, `InputActionEvent`, and `InputActionState`. New code should include `InputFlow.h`, not depend on the old implementation header directly. Existing `EnhancedInput.h` consumers, `enhanced::InputSystem`, JSON schema v1 (`input/actions.json`) and `AIVERSE::lib_input` continue to work unchanged.

**Documentation:** `docs/enhanced-input.md`, `skills/ui/aiverse-input/SKILL.md`, and root `AGENTS.md` now identify AIVerse InputFlow as the canonical public API. Legacy file names/IDs are intentionally preserved for source and asset compatibility.

**Evidence:** GitHub source commits: `8ccca5eb83ba82897599dd9711872016e4b1a6ab` (renamed implementation and alias), `b4b7b3ec5ddb3f8637c3252237e640b1252608f4` (new facade). No tests run, no CI pass claimed.

**Known risks/limitations:** Tooling, editor window, FPS/RTS clients and platform adapters still need to adopt `inputflow::InputFlow` and visible InputFlow labels; old API remains supported. The class-name change should be compile-verified in the later test phase.

**Next agent / Stage 44:** Update `engine/platform/EnhancedInputAdapter.h`, `EnhancedInputConfig.h`, `EnhancedInputPresets.h`, `EnhancedInputUsers.h`, `EnhancedInputUserProfiles.h`, `apps/fps/client.cpp`, `apps/client/ClientInputRouting.h`, `apps/editor/windows/InputWindow.cpp` and `GameWindow.cpp` to include `aiverselib/gameplay/InputFlow.h` and use the `inputflow` namespace/branding; retain compatibility paths, stable IDs and JSON schema. Update tests, catalog, and this handoff after that stage. Testing remains deferred.
