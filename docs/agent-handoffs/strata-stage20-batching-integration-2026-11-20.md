# STRATA Stage20 — batching integration (2026-11-20)

## Source and scope
User authorized integrating batching into STRATA and main only when ready.
Branch: `codex/strata-batching-integration`, isolated worktree `/private/tmp/aiverse-strata-batching`.
STRATA source: `60820d5485696053e30ccb9e769f06fcc19d782e` (PR70).
Batching source: `b2b5c180d8cf1fa0b3c87b445167a6d77a7d0057` (Stage142 / PR94, including its ancestor stack).
Implementation merge: `8d383ba84f54f2b90e19cd354f3744f56efd713a`.

## Conflict review
Only Gfx.h and GfxMetal.mm conflicted. Preserved both independent additive APIs:
STRATA/main GPU scope timing and batching model-pass raw GPU counters; preserved both cmath/iterator includes.
Renderer3D.h/Renderer3DModels.cpp merged automatically; further native validation required.
No production flags, gameplay/save formats or existing API semantics intentionally changed.
Batching experiments remain opt-in; Stage142 reports GPU regression, not a universal performance win.

## Verification
`git diff --check`: PASS. Native build, CTest and current combined GPU tests: NOT RUN yet.
Prior STRATA Mac results and batching Stage142 captures apply to their original trees only.
CI for old PR70 was still queued at inspection; no CI pass claimed.

## Next step
Merge latest origin/main after this handoff commit; review shared rendering conflicts, build and run native tests, record exact tested SHA and results, then fast-forward the existing STRATA integration branch. Main merge remains conditional on readiness. Preserve uncommitted work in the primary Studio checkout.

## Stage20.1 — current main reconciliation
Main `5aba82c35` merged without textual conflicts in implementation `5cd7055b08a9046bd5497646c57609579f75972b`. Preserves physical sky/local fog and Release retirement test fixes. No manual source edits in this merge. Native combined checks are next; NOT RUN at this checkpoint.
