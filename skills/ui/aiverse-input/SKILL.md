---
name: aiverse-input
description: Build genre-independent Enhanced Input Actions, Mapping Contexts, Modifiers, Triggers and rebinding profiles in AIVERSE C++ projects. Preserve legacy input helpers.
---

# AIVERSE Input (Enhanced Input, presentation only)

Read \`docs/enhanced-input.md\` and \`docs/enhanced-input-agent-handoff.md\` **before changing the input system**. Read \`AGENTS.md\` and \`libraries/catalog.json\` in engine root. The handoff contains stage status, all changed files, test coverage, caveats and next-agent instructions. The documentation MUST be updated **after each stage** — no stage is finished without its handoff section.

## Pick the existing mechanism first

- Legacy \`libraries/include/aiverselib/gameplay/Input.h\`: single-context \`resolveInput\`, integer \`deadzone\`, \`ControlGroups\`, \`updateSelection\`, \`DragGesture\`. Never remove or silently break it.
- New \`libraries/include/aiverselib/gameplay/EnhancedInput.h\`: \`aiverse::lib::gameplay::enhanced::InputSystem\`, typed Input Actions and 3D values, runtime Mapping Context activation/priorities, mapping/action Modifiers and Triggers, lifecycle events, key rebinding. Header-only C++20, **no platform backend**.
- \`engine/platform/EnhancedInputAdapter.h\`: bridge from \`platform::Event\` and \`GamepadState\` into generic Controls. Focus loss cancels pressed state. Host must not poll gamepads twice per frame.
- \`engine/platform/EnhancedInputConfig.h\`: validated \`Json\` project mapping asset \`readAsset/installAsset/writeAsset\` and per-user \`readProfile/applyProfile/writeProfile\`. Includes engine \`Json\` (not standalone lib-only code).
- FPS reference: \`apps/fps/client.cpp\` maps Action state/events to existing \`fps::Input\` network commands, without changing authoritative protocol.
- For settings read \`docs/gameplay-settings.md\`; do not invent keys in \`libraries/spec/gameplay.json\`.

## Implementation workflow

1. Identify if the feature is already an Input Action or an ECS gameplay mechanic. Use an action/context first; NEVER duplicate key handling per genre or write keyboard/mouse logic into ECS systems.
2. Define semantic actions (\`Fire\`, \`Move\`, \`PanCamera\`, \`Interact\`) with Boolean / Axis1D / Axis2D / Axis3D. Define physical mappings and optional modifier/trigger chains in a named context, not hardcoded inside a simulation tick. Compose UI, modal, gameplay and vehicle contexts by priority; UI/modal consume input before gameplay.
3. Use \`platform::EnhancedInputAdapter::onEvent\` for key/mouse/wheel/touch, \`pollGamepads\` once per presentation frame, and \`InputSystem::update(dt)\` to evaluate actions. Consume both \`ActionState\` (held level) and lifecycle events (\`Started\`, \`Ongoing\`, \`Triggered\`, \`Completed\`, \`Canceled\`). Preserve very short taps/one-shot actions by queuing them until the next command tick if needed.
4. Author an optional \`<project>/input/actions.json\` with stable mapping IDs; load via \`platform::enhanced_config::readAsset\` and transactionally \`installAsset\`. User-specific key overrides go to a profile (NOT \`rules.json\`), e.g. \`platform::userDataDir()\`. \`applyProfile\` validates every mapping target before applying; protect users from invalid / future profiles.
5. Convert actions into validated **Commands** before entering the simulation. The sim and ECS operate on integer/Fixed inputs and replicate Commands; raw floats and device events stay in presentation. Local camera and selection can remain local state; authoritative actor motion must not depend on client wall time.
6. For C++ native modules, \`aiverse_native_use_libraries(aiverse_native_ID input)\` / \`AIVERSE::lib_input\`. The pure library is usable without Lua or engine code. Include \`<aiverselib/gameplay/EnhancedInput.h>\`. Platform/JSON helpers additionally need engine headers from host, not from the pure library target.
7. Preserve old game controls and network formats during migration; migrate clients incrementally. Test focus loss, pointer lock, UI capture, two contexts using the same key, simultaneous chords, invalid rebinds, gamepad disconnect, deadzones and mouse deltas.
8. **Immediately after each stage**, append the changed files/APIs, verification commands and exact results, known unverified platforms, open issues and a precise next-agent checklist to \`docs/enhanced-input-agent-handoff.md\`. Keep earlier stages. Update this skill/catalog/docs together when the library API changes.

## API sketch

\`\`\`cpp
namespace ei = aiverse::lib::gameplay::enhanced;
ei::InputSystem input;
input.defineAction({"Move",ei::ValueType::Axis2D,{ei::Modifier::normalize()}});
input.defineAction({"Fire",ei::ValueType::Boolean});
ei::MappingContext gameplay{"Gameplay"};
gameplay.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::W)},
              {ei::Modifier::swapXY()});
gameplay.bind("Fire",{ei::Device::MouseButton,0},{},{ei::Trigger::pressed()});
input.registerContext(std::move(gameplay));
input.activateContext("Gameplay",0);
input.setControl({ei::Device::MouseButton,0},1);
for(const auto& event:input.update(1.f/60)) { /* queue commands on Triggered */ }
\`\`\`

Use \`Modifier::scale/negate/deadZone/swapXY/normalize\`, \`Trigger::down/pressed/released/hold/tap/chord\`, \`InputSystem::rebind(context,mappingId,control)\`. For mouse motion use \`addDelta\`; its values are intentionally NOT clamped to [-1,1]. A "hold" trigger fires once per press, while a "down" trigger fires while active; multiple trigger requirements on one mapping are AND-gated.

## Mandatory validation

\`\`\`sh
cmake -S tests/libraries -B /tmp/aiverse-input-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aiverse-input-tests --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests
ctest --test-dir /tmp/aiverse-input-tests --output-on-failure -R library_enhanced_input
python3 tools/libraries/catalog.py check
python3 tests/libraries/run.py
python3 tests/libraries/native_consumer.py
\`\`\`

For FPS/client integration also build \`aiverse_fps_client\`, run sample tests and full engine verification per \`AGENTS.md\`. Run GCC and Clang/MSVC/Apple CI where possible; do not claim a platform has been verified without success. Review \`docs/enhanced-input-agent-handoff.md\` for current stage and outstanding work.
