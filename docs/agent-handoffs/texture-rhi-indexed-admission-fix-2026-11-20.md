# Indexed VT ICB admission fix — 2026-11-20

Branch `codex/texture-completion`, implementation
`b17dba1a02066fd23d9764a5e07fbc16b9e27e63`; previous test gates
`a6d32f7b8eb02302eb0242ba656c1c7b52defe8b`, failure handoff `95692781d`.
See `texture-rhi-final-gates-2026-11-20.md` for the reproduced failure.

## Fix and compatibility

`engine/render/Renderer3DModels.cpp::prepareGpuModelDraws` resolves each
candidate VT binding once, removes indexed bindings before preflight/cache
construction, and counts only accepted dynamic/LOD/total candidates.
The legacy per-command ICB ABI carries 16 bytes of VT parameters; indexed
sampling needs 48 bytes and has no ICB shader permutation. These draws now
use the ordinary renderer without a doomed backend submission each frame.
The strengthened real Renderer3D regression also requires zero eligible
GPU items. Legacy VT/PBR/graph ICB remains enabled only by existing opt-ins;
no default flag, public API, shader ABI, save/network or ECS state changes.
Docs and Texture RHI skill describe this supported fallback.

## Actual targeted validation

All builds, reports, temp files and module/Python caches are on KINGSTON.
Build root: `/Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug`.
Source: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/texture-completion`.
Environment: `TMPDIR=<build>/tmp`,
`CLANG_MODULE_CACHE_PATH=<build>/module-cache`,
`PYTHONPYCACHEPREFIX=<build>/python-cache`.

- Full Debug default-target build including editor and all registered test
  binaries PASS (`build-full.log`). Existing warnings: EXR test unused u64,
  SpectraGraphWindow unused ed/validId. No compiler errors.
- Full incremental rebuild after this fix PASS (`build-fix.log`), no warnings.
- `bash run-tests.sh --no-build --build-dir <build> --filter
  '^metal_renderer_(indexed_vt|graph_vt_gutter)_gpu$' --ctest-jobs 1`: 2/2 PASS,
  3.01 s; report `test-reports/20261120-143651-94146`.
  Legacy: actual graph/mixed VT ICB commands=2, errors=0 at every probe.
  Indexed: eligible=0 asserted; commands=0, errors=0 at every probe.
  Both have exact direct-vs-opt-in image parity and independent scene oracle.
- `git diff --check`: PASS.
- Complete registered regression launched with `bash run-tests.sh --quick
  --no-build --build-dir <build> --ctest-jobs 4`; report
  `test-reports/20261120-143715-97790`, aggregate `final-regression.log`.
  RUNNING at this checkpoint (291/300 passed; no failures). The --quick flag
  skips unrelated extra shell suites, not any registered CTest. Eight Metal
  tests are RUN_SERIAL; generated CTest properties inspected individually.

External CI for previous PR #95 remains queued:
https://github.com/chromealex/AIVERSE/actions/runs/44752147600
https://github.com/chromealex/AIVERSE/actions/runs/44752147644
No GL/GLES/Vulkan runtime PASS or performance claim. The local Docker daemon
is unavailable; native shader compilers passed in the prior gate stage.

## Next action

Wait for the already running full regression, inspect failures/skips and
append its exact result before merging the draft follow-up PR. Do not run
a competing full regression. Then merge ordinarily (user authorized main),
verify merge SHA and inspect focused CI. Original draft #21 head
`9145e6efe72a31306f7197f32f080e29908beb3b` is already an ancestor of this
main-based branch; it can be closed as superseded after the final merge.
