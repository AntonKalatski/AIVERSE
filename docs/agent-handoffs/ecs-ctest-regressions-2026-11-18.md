# ECS combat + FPS COW CTest triage — 2026-11-18

Branch: `feature/universal-entity-inspector-fps` (shared with other agents).
Status: **source fixes committed; local native CTest not run in this session**.

## Reported Mac Studio failure

User ran build targets:
`aiverse_ecs_projectile_composition_test`,
`aiverse_ecs_hitscan_resolution_test`,
`aiverse_ecs_hitscan_selection_test`, `aiverse_fps_tests`.

CTest filter:
`^(fps|ecs_projectile_composition_test|ecs_hitscan_resolution_test|ecs_hitscan_selection_test)$`.

Reported failures:
- projectile composition: `shared projectile sweep lost nearest-hit tie semantics`;
- hitscan resolution: `wrong closest hit fraction`;
- FPS: `tests/fps/tests.cpp:195`, checksum unexpectedly changed resource sharing;
- hitscan selection: `Not Run` (executable launch/build issue, not assertion failure).

## Code inspection and fixes

1. `world::intersect` in `engine/world/SpatialWorld.cpp` computes first contact with `Fixed::operator/`; `Fixed::fromRatio` instead rounds to nearest, while division truncates. This is an expectation mismatch, not a reason to change the shared collision solver.
   - Existing commits: `dfef673b` fixes projectile contact expectations; `9e5263a6` hitscan resolution fraction + derived point; `940064fa` hitscan selection expectation.
   - Deterministic hit arbitration itself was not changed.
2. `modules/fps/FpsModule.cpp` used mutable `state(sim)` inside `hashState`/authoritative copy. COW snapshots detach `State` when `world.resource<State>()` is called. Existing commit `383b9c90` uses `resourceConst<State>()` for checksum and materialization.
3. Inspecting the latest source found two accidental literal `\\n` escapes inserted *inside C++ comments*, hiding subsequent `const auto` declarations:
   - `tests/ecs_projectile_composition_test.cpp`: fixed by `7e183a43c2bcebb5e77b6a056bb1685e04cdb700`.
   - `tests/ecs_hitscan_selection_test.cpp`: fixed by `885abcfb9c4e3634a1ed87b9cb0bb71cec0a140e`.
   These commits only convert the accidental escapes into real newlines; no gameplay or test condition changes.

## Required next validation

On Mac Studio, from the repository root:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --parallel 4 --target \
  aiverse_ecs_projectile_composition_test \
  aiverse_ecs_hitscan_resolution_test \
  aiverse_ecs_hitscan_selection_test \
  aiverse_fps_tests
ctest --test-dir build/ecs-test --output-on-failure \
  -R '^(fps|ecs_projectile_composition_test|ecs_hitscan_resolution_test|ecs_hitscan_selection_test)$'
```

If `ecs_hitscan_selection_test` remains `Not Run`, inspect the build output and
`build/ecs-test/aiverse_ecs_hitscan_selection_test` (with multi-config generators,
inspect the active configuration directory). Check `ctest --test-dir build/ecs-test -N -R ecs_hitscan_selection_test`.
Do not count `Not Run` as a passed regression.

The agent's container had no local AIVERSE checkout and
`git ls-remote https://github.com/chromealex/AIVERSE.git` failed DNS resolution,
so this handoff does **not** certify a successful macOS build or CTest.
Preserve shared-branch concurrent agent work; do not force-push.

## Completed follow-up — final hitscan selection assertion (2026-11-18)

User's Mac Studio rerun after rebuilding reported **3/4 passed**, with
`ecs_hitscan_resolution_test` and `fps` passing; the only remaining
failure was `ecs_hitscan_selection_test` throwing `unexpected on-hit effect`.
(The fourth passing test was `ecs_projectile_composition_test`.)

Root cause: `resolveOrderedProjectileImpact` unconditionally invokes its
`AfterDirect` hook immediately after `DirectHit` regardless of whether
the weapon has any optional effect. The test incorrectly supplied a
throwing callback as if the hook would be skipped when no on-hit effect
is configured. Changing the common library would break existing
cross-genre ordered impact expectations.

Fix commit: `28899abf48d27506a364cf5e4ca213a08fdf4663`
in `tests/ecs_hitscan_selection_test.cpp`. The empty on-hit stage is
implemented as an effect-free hook that counts invocations and asserts
its position after direct damage. The blocked-hit case also asserts
no hooks run. No gameplay library, collision policy, or FPS/RTS logic
was modified.

**Validation pending:** rerun the user-side macOS CMake target and the
four-test CTest filter below. This agent had GitHub-source access but no
native Mac build/CTest; do not claim all four tests are green until
user/CI confirms.

```sh
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake --build build/ecs-test --parallel 4 \
  --target aiverse_ecs_hitscan_selection_test
ctest --test-dir build/ecs-test --output-on-failure \
  -R '^(fps|ecs_projectile_composition_test|ecs_hitscan_resolution_test|ecs_hitscan_selection_test)$'
```
