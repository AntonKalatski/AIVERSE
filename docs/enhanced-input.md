# Enhanced Input (Unreal-style, AIVERSE API)

AIVERSE has a genre-independent, action-based **presentation input** layer inspired by Unreal Engine's Enhanced Input. It is not a clone of Unreal's C++ classes or asset binary formats.

## Runtime pipeline

\`\`\`
platform::Event / GamepadState
  -> platform::EnhancedInputAdapter (keyboard, pointer, wheel, touch, pads)
  -> lib::gameplay::enhanced::InputSystem
       active Mapping Contexts, sorted by priority
       physical Control -> mapping Modifiers -> mapping Triggers
       Action Value aggregation -> action Modifiers -> action Triggers
       Started / Ongoing / Triggered / Completed / Canceled
  -> gameplay presentation code and UI
  -> validated / tick-stamped integer or Fixed Commands
  -> ECS systems / authoritative simulation
\`\`\`

Input processing is **not** ECS authoritative simulation. Never feed float device state directly to deterministic ECS. ECS systems consume semantic commands and own the rules; UI and remapping stay local. A single \`Fire\` action can be converted into FPS, RTS, vehicle, turret or UI commands without duplicating physical-key handling.

Files:
- \`libraries/include/aiverselib/gameplay/Input.h\`: legacy \`InputPhase\`, \`Binding\`, \`resolveInput\`, \`deadzone\` and selection helpers (unchanged).
- \`libraries/include/aiverselib/gameplay/EnhancedInput.h\`: header-only pure C++20 core, target \`AIVERSE::lib_input\` / native \`aiverse_native_use_libraries(target input)\`.
- \`engine/platform/EnhancedInputAdapter.h\`: OS event and gamepad conversion; requires platform hosting.
- \`engine/platform/EnhancedInputConfig.h\`: strict JSON action assets and per-user rebinding profiles; requires the engine \`Json\` type, so native modules link engine interfaces if they need it.
- \`apps/fps/client.cpp\`: FPS reference use; maps action state to the unchanged \`fps::Input\` command.
- \`docs/enhanced-input-agent-handoff.md\`: stage-by-stage implementation and continuation record; **read this before modifying**.

## C++ basics

\`\`\`cpp
#include <aiverselib/gameplay/EnhancedInput.h>
namespace ei = aiverse::lib::gameplay::enhanced;
ei::InputSystem input;
input.defineAction({"Move",ei::ValueType::Axis2D,{ei::Modifier::normalize()}});
input.defineAction({"Fire",ei::ValueType::Boolean});
ei::MappingContext game{"Gameplay"};
game.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::W)},
          {ei::Modifier::swapXY()});
game.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::S)},
          {ei::Modifier::swapXY(),ei::Modifier::negate()});
game.bind("Fire",{ei::Device::MouseButton,0},{},{ei::Trigger::pressed()});
input.registerContext(std::move(game));
input.activateContext("Gameplay",0);
aiverse::platform::EnhancedInputAdapter adapter(input);
// In the platform event callback:
adapter.onEvent(platformEvent);
// Once per presentation frame (if the app owns gamepad polling):
adapter.pollGamepads();
for(const auto& e:input.update(float(dt))) {
    if(e.action=="Fire" && e.phase==ei::Phase::Triggered) { /* queue a command */ }
}
const auto* move=input.state("Move");
if(move && move->active) { /* quantize move->value.x/y to a Command */ }
\`\`\`

\`Control\` is \`{Device,code,deviceIndex=0}\`. Gamepad index 0..3. The mouse axis is relative motion (X/Y), mouse wheel (X/Y) is a one-frame pulse. Axis values can exceed 1 for mouse deltas; add \`normalize\` intentionally to a movement action. Supported action types: Boolean, Axis1D, Axis2D, Axis3D. Mapping and Action modifiers: Scale, Negate, DeadZone (rescaled), Swizzle, Normalize. Triggers: Down, Pressed, Released, Hold(seconds), Tap(seconds), Chord(action name).

A single update may deliver multiple lifecycle events for one action (Started + Triggered; later Triggered + Completed). A tap entirely between updates can still produce a one-frame Triggered pulse. Hold is one-shot per press and needs \`update(dt)\` to advance. Multiple triggers on a mapping are AND-gated. Contexts are ordered by descending numeric priority; equal priority context names are ordered lexicographically, with mapping declaration order inside a context. Active higher-priority bindings consume controls by default; use \`consume=false\` for intentional sharing. \`deactivateContext\`, \`cancelAll\`, pointer lock changes and lost focus must not leave held input.

## Project-defined action assets

The optional FPS asset lives at \`<project>/input/actions.json\`. If it is absent the FPS reference client uses its built-in default FPS mapping. If present and valid it **replaces** those defaults completely. Do not ship an incomplete asset unless you intentionally omit those actions.

Example:
\`\`\`json
{
  "version": 1,
  "actions": [
    { "name": "Move", "type": "axis2D", "modifiers": [{ "type": "normalize" }] },
    { "name": "Aim", "type": "bool" },
    { "name": "Fire", "type": "bool" }
  ],
  "contexts": [
    {
      "name": "Gameplay", "priority": 0, "active": true,
      "mappings": [
        { "id": 1, "action": "Move", "control": { "device": "keyboard", "code": "W" },
          "modifiers": [{ "type": "swizzle", "order": [1, 0, 2] }] },
        { "id": 2, "action": "Move", "control": { "device": "keyboard", "code": "S" },
          "modifiers": [{ "type": "swizzle", "order": [1, 0, 2] }, { "type": "negate" }] },
        { "id": 3, "action": "Aim", "control": { "device": "mouseButton", "code": "right" } },
        { "id": 4, "action": "Fire", "control": { "device": "mouseButton", "code": "left" },
          "triggers": [{ "type": "pressed" }, { "type": "chord", "action": "Aim" }] }
      ]
    }
  ]
}
\`\`\`

\`device\` values: \`keyboard\`, \`mouseButton\`, \`mouseAxis\`, \`mouseWheel\`, \`gamepadButton\`, \`gamepadAxis\`, \`touch\`. Keyboard codes follow \`platform::Key\` names (\`W\`, \`LeftShift\`, \`Num1\`, etc.); mouse buttons: \`left\`, \`right\`, \`middle\`, \`x1\`, \`x2\`; mouse axis and wheel: \`x\`/\`y\`; pad buttons: \`A\`, \`B\`, \`X\`, \`Y\`, \`LB\`, \`RB\`, etc.; pad axes: \`LX\`, \`LY\`, \`RX\`, \`RY\`, \`LT\`, \`RT\`. \`deviceIndex\` defaults to 0, max 3 for pads. Numeric codes are accepted but string codes are preferred for editor usability.

Action \`type\`: \`bool\`, \`axis1D\`, \`axis2D\`, \`axis3D\`. Modifiers: \`scale\` with \`x/y/z\` numbers, \`negate\`, \`deadzone\` with \`threshold\` (0..0.999), \`swizzle\` with \`order\` (3 source indices), \`normalize\`. Triggers: \`down\`, \`pressed\`, \`released\`, \`hold\` or \`tap\` with \`seconds\`, \`chord\` with \`action\`. Each mapping may have \`consume\` (default true). Mapping \`id\` should be a stable positive integer for rebinding profiles. Strict schema rejects unknown fields, duplicate names/ids, missing actions, unsupported codes, invalid ranges and unresolved chord actions. Asset loading is transactional: a bad asset must leave the current \`InputSystem\` intact.

## Rebinding profiles

The user profile is independent of the project's authored asset and **only** stores mapping-id overrides:

\`\`\`json
{
  "version": 1,
  "rebindings": [
    { "context": "Gameplay", "id": 1,
      "control": { "device": "keyboard", "code": "Up" } }
  ]
}
\`\`\`

Use \`platform::enhanced_config::readProfile\`, \`applyProfile\`, \`writeProfile\` and \`InputSystem::rebind(context,id,Control)\`. A profile validates all context/ID targets before applying anything. The FPS sample reads/writes local profiles in \`platform::userDataDir()\` under \`enhanced-input-<stable-project-hash>.json\`; this never mutates project assets. \`App::rebindInput\` is the reference persistence hook for a future input-settings UI. When manually saving user settings on Web, call \`platform::syncUserData()\`.

## Reusing in other genres

- **RTS:** \`Select\`, \`AttackMove\`, \`CameraPan\` actions become local selection or validated RTS Commands. Separate \`UI\`, \`Modal\`, \`Gameplay\`, \`BuildingPlacement\` mapping contexts. Do not leak UI clicks into the world.
- **FPS:** fixed-tick \`fps::Input\` still handles sequence/viewTick/weapon actions; input device read and modifiers stay presentation-side.
- **Vehicles:** \`Steer\`, \`Throttle\`, \`Brake\`, \`Interact\` can combine digital keys, pedals and pad axes using actions, without a genre-specific platform backend.
- **Editor:** viewport, gizmo, text fields, modal dialogs and play-test should each own a mapping context. Respect ImGui/RmlUi capture before gameplay and preserve text composition.

## Validation and outstanding work

Run:
\`\`\`bash
cmake -S tests/libraries -B /tmp/aiverse-input-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aiverse-input-tests --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests
ctest --test-dir /tmp/aiverse-input-tests --output-on-failure -R 'library_enhanced_input'
python3 tools/libraries/catalog.py check
python3 tests/libraries/native_consumer.py
\`\`\`

Plus FPS build and sample matches on all supported platforms, plus engine full tests per \`AGENTS.md\`. Do not mark any platform verified without a successful build and tests.

Known remaining items: editor mapping UI and live rebind capture, RTS/default client context migration, hosted viewport event arbitration, a mobile touch gesture mapper, multiple local players, advanced chord/conflict semantics, configuration package generator validation and final cross-platform CI. These should be tracked in \`docs/enhanced-input-agent-handoff.md\` after each stage.

## Authoring mappings in AIVERSE Editor

Open **Logic → Input Actions** (window ID `input`) for any native project. The dockable editor window reads and writes `<project>/input/actions.json`. It shows editable Actions (Bool/Axis1D/2D/3D, Action modifiers and triggers), Mapping Contexts (enabled/priority), and their bindings (Action, device, code, device index, consume, mapping modifiers/triggers). Keyboard bindings support click-to-capture; Escape cancels without modifying the binding. A conflict warning appears for duplicate physical controls within a context. The window is registered automatically through `AIVERSE_EDITOR_WINDOW`.

**Save** validates the entire asset using the strict `readAsset` parser before writing. It writes a temporary JSON file and creates `.bak` before replacing an existing asset; a failed validation leaves the original untouched. The FPS sample regards this as an entire mapping override — creating an incomplete FPS asset can disable unmapped actions. Use **Reload** to abandon unsaved edits. Project-owned mappings and per-user rebinding profiles are intentionally separate. The current window edits authored project assets, **not** a running player's saved profile.

Capture temporarily suppresses global editor shortcuts (e.g. F5) and cancels when the window is no longer the selected dock tab. The editor's own viewport/tool shortcuts and the hosted game's legacy input routing are not yet fully converted; do not claim complete input capture arbitration until the RTS/hosted client stage is validated.

## RTS client integration

The generic RTS client uses `apps/client/ClientInputRouting.h` for `AttackMove`, `Patrol`, `Repair`, `Reclaim`, `Stop`, `Hold` and `FocusCamera`. Each action resolves from `RTS.Gameplay` Mapping Context; optional project `input/actions.json` overrides the complete default shortcut set, as in FPS. RmlUi/Modal capture runs first and semantic shortcuts cannot pass through captured UI. Scripted command hotkeys retain first refusal, followed by semantic Actions -> existing `issue(Command)` calls. Numeric control groups and client pointer gestures remain backward-compatible.

## In-game input rebinding (FPS example)

Press **F10** while playing the FPS sample to open Input Settings and release pointer lock. Select **Rebind** beside any mapping and press a new keyboard key, mouse button or gamepad button; moving a pad axis past 0.8 can bind that axis. The UI displays duplicate-control warnings within each context. Changes use the existing `rebindInput` and `platform::syncUserData` profile persistence; they do **not** change the project's authored `input/actions.json`. Escape cancels active capture. Close Settings and click the viewport to resume locked-pointer gameplay. Editor-side binding authoring remains in the Logic → Input Actions window.

## Complete default mapping presets

The shared engine header `engine/platform/EnhancedInputPresets.h` provides `enhanced_presets::fpsReference()` and `rtsReference()`, returning editable full assets with stable mapping IDs. Both reference clients use these presets, and **Logic → Input Actions → Use FPS defaults / Use RTS defaults** fills the editor buffer with the corresponding complete scheme. Save to write `<project>/input/actions.json`. Existing user rebinding profiles continue to apply by mapping ID. Avoid removing/reordering mapping IDs in a published project without planning a profile migration.
