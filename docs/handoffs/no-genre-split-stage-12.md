# No genre split, stage 12 (2026-11-22)

Branch `feature/aiv20261121_no-genre`.

## Done

- `modules/` is removed. What was left moved into the engine:
  - Bunker Wars computer player `modules/rts/RtsBwAI*` -> `engine/game/BwAI*` (rules `ai.controller` `"bw"`, works
    in any project); its packaged-client stub -> `engine/game/disabled/BwAI.cpp`.
  - `modules/rts/RtsGameExtensions.cpp` -> `engine/game/GameExtensions.cpp`.
  - `modules/strategic/Strategic.*` -> `engine/game/Strategic.*`.
  - Native-module CMake `modules/rts/native/Build.cmake` -> `engine/game/native/Build.cmake`.
  - `modules/rts/KINGSTON_INTEGRATED.md` -> `docs/kingston-integrated.md`.
- Skills and docs no longer call any host "legacy" or list host debt G3: rules keys `actor.*`,
  `data.worldSpace`, `"module": "game" / "actor"`.

## Breaks

Native projects that included `modules/rts/native/Build.cmake` directly must include
`engine/game/native/Build.cmake`.

## Tests

Pending (Mac run).
