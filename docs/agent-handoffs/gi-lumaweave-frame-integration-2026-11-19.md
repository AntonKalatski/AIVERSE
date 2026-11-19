# GI + LumaWeave integration — agent handoff (2026-11-19)

Branch: `feature/gi-lumaweave-frame-integration-20261119` (forked from `feature/lumaweave-massive-lighting` at `4a560575417022b4e78fc4c1b677eccce99f6f8f`). Never modify `main` from this stage. Indirect GI and direct LumaWeave remain separate contributions; no double light.

## Stage 1 — geometry-free mobile fallback (SOURCE COMMITTED; NOT NATIVE TESTED)
Source commit: `ca77cb89ee6f51df805b069f852b208d7ee33a96`.
Changed: `engine/render/LumaWeaveNativeCoordinator.h`.

- Add `prepareFrame(const GiGpuBvh* geometry, uint64_t revision, ...)` with a nullable geometry/revision-zero pair. The original `const GiGpuBvh&` overload stays source compatible and forwards to this overload.
- The Metal ray-traced path requires BOTH non-null geometry and nonzero geometry revision. Without geometry it cannot enter Metal, instead the existing explicitly selected mobile/legacy raster path can receive the same validated LumaWeave registry without a dummy BVH. Strictly reject mismatched pointer/revision pairs.
- Neither ECS, LightSystem, RHI, nor the GI probe runtime was modified. Legacy-vs-GPU exclusivity and overflow source-loss diagnostics remain unchanged.

Actual checks: GitHub file read and GitHub source update acknowledged. **NOT RUN:** native C++ compiler, CMake, CTest, Metal/GLSL compilation, GPU screenshots/perf, benchmarks. Source has not been validated by a native build.

Environment/rebase warning: GitHub comparison on 2026-11-19 showed the source feature branch 467 commits behind `main`, with substantial concurrent renderer changes. Do not pretend it is rebased/merged. Preserve main's renderer, camera, texture, FX changes when eventually integrating; resolve a real three-way merge on a native checkout. No branch merge has been attempted.

Next: wire an OPTIONAL GiGpuCascadeRuntime into native per-view driver, with actual fence polling, explicit bounce-source sync, per-view GI overlay reset (Renderer3D::beginScene does not clear it), and fail-closed presentation when source sync is deferred. Then add targeted CPU/source regression fixtures, update AGENTS.md and acceptance runner, and perform ONE native validation pass after code freeze. Record exact test SHA/results, not historic 33/33.
