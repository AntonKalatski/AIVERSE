# ECS real RTS/FPS integration replay gate — handoff 2026-11-18

Branch: `feature/universal-entity-inspector-fps`. Shared with other agents:
do not force-push, overwrite their concurrent files or change engine default
storage mode as part of this regression step.

## Bounded completed stage: actual genre adapter save/replay CTest

Implementation commits:
- `d5a719fddc3f71d1b61f0518bf1b62cdf7b9c9d9` — add `tests/ecs_real_genre_replay_test.cpp`
- `8c4fdf733a57d8b6413df4587abf6a367796ea31` — register the test in root `CMakeLists.txt` as `ecs_real_genre_replay_test`

Test uses actual `game::RtsModule` with `templates/mushrooms` garrison commands
and actual `fps::Module` with `examples/fps/arena` movement/fire commands.
Two independent instances accept same-tick commands in opposite arrival order,
then compare every-tick checksums. At tick 20, snapshot + disk save are made,
save v9 is asserted, loaded into a new real module, byte-serialization
roundtrip is checked, malformed trailing-byte disk load is rejected without
changing state, and from tick 20 through 45 peer/reload replay hashes are
compared. Restore original snapshot and assert per-tick replay hashes.

Variants: RTS deep/COW; FPS legacy storage deep/COW; FPS `ecsAuthority=true`
deep/COW. FPS default remains OFF. Results are compared between variants
of **the same genre**; FPS and RTS checksums are not expected to match.

This is an integration **regression gate** on production module entry points;
it does NOT prove that all old FPS/RTS mechanics have migrated to the shared
library, that native ECS v10 state is present in either default module, nor
that the same firing implementation is used for every weapon. For those
independent checks continue to run `ecs_cross_genre_weapon_pipeline_test`,
`ecs_cross_genre_projectile_test`, `ecs_hitscan_resolution_test`,
`ecs_native_rts_integration_test`, `fps` and `fps_transport`.

## Compatibility and risks

- No new public C++ API and no production logic changes.
- The test requires both real static libraries (`aiverse_game`, `aiverse_fps`).
- Source fixtures are loaded from `AIVERSE_SOURCE_DIR`, not process cwd.
- On-disk legacy save-format checksum must remain v9 without registered
  native ECS schemas.
- A FAIL from the new test should be investigated: command ordering, derived
  caches, v9 serialization, FPS State/ECS parity or COW may now disagree.
- The test intentionally asserts exact save bytes after round-trip and
  cross-storage per-tick checksums; if mismatch is legitimate, explain it
  and update the compatibility contract rather than weakening assertions.

## Verification

- GitHub source files were read and modified using the connected repository.
- Local `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed:
  `Could not resolve host: github.com`. No checkout in this worker.
- **Native compilation: NOT RUN. CTest: NOT RUN.**
- User's immediately preceding Mac Studio run DID pass 4/4 older tests,
  but that result predates this newly added test.
- No CI run has been asserted as green for this stage.

## Next agent / Mac Studio commands

```bash
cd ~/Projects/AIVERSE
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --parallel 4 --target \
  aiverse_ecs_real_genre_replay_test \
  aiverse_ecs_cross_genre_projectile_test \
  aiverse_ecs_cross_genre_weapon_pipeline_test \
  aiverse_ecs_native_rts_integration_test \
  aiverse_fps_transport_tests \
  aiverse_fps_tests
ctest --test-dir build/ecs-test --output-on-failure \
  -R '^(ecs_real_genre_replay_test|ecs_cross_genre_projectile_test|ecs_cross_genre_weapon_pipeline_test|ecs_native_rts_integration_test|fps|fps_transport)$'
```

After observing output, fix any real parity/serialization regressions in the
same branch. Add the next handoff with exact CTest result and code SHAs;
do not hide failures by removing coverage. Future stage: build a public
persistent native shared-weapon pipeline (registered ECS components and
queues, v10 disk codec, live RTS/FPS adoption matrix) without duplicating
authority or silently changing legacy v9 save behavior.
