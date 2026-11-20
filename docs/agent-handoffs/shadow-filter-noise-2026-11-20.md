# Shadow filter noise — 2026-11-20

## Stage 1: diagnose and gate spatial random rotation (incomplete visual fix)

Branch: `feature/studio-html5`. Base: `2f9c1b01ca07140dfecba2cf7d994881f1b5b5cf`.
Code: `7dd7edb16f48e355a236a47c5bd4c9144a56d8c3`.
Paths: `engine/render/Shaders.h`, `tests/render/temporal-pcss.md`.
Both GLSL/MSL now require the existing fractional quality.x temporal capability for spatial rotation. Low, sample budgets, receiver-plane bias, PCSS blocker search, public API/save format remain unchanged.

Validation performed on the code tree above before commit:
- `python3 tests/render/check_model_lod_shaders.py`: PASS 32 GLSL/GLES links.
- CMake Debug build in `/private/tmp/aiverse-shadow-noise-20261120`: PASS targets aiverse_hdr_scene, aiverse_hdr_gl, aiverse_hdr_policy. Configured with HDR tests ON and editor/tools/client/server/headless OFF.
- `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter '^hdr_policy$'`: PASS 1/1 after enabling AIVERSE_BUILD_TESTS; first attempt had no registered tests (exit 8). Report `test-reports/20261120-134245-5185/summary.txt`.
- Contact fixture: Metal high/fixed/medium/low/low-fixed and desktop GL high captures PASS; `python3 tests/render/verify_contact_shadows.py /private/tmp/aiverse-shadow-noise-20261120/captures` PASS (Metal widths 3/11/21, GL 5/12/21, medium 4/11/21). Metal pipelineFailures=0.
- Visual inspection of contact-new.png reveals distinct bands along wide penumbra. User explicitly rejects these bands. This stage is NOT a finished visual fix.
- AIVERSE_TAA_VERIFY=1 did not produce a valid verification despite process exit 0: terrain high MSL compile reports 17 samplers (limit exceeded), no TAA PASS result. Treat as FAIL/unverified, unrelated terrain shader limitation; do not infer success from exit code.
- GPU launches required sandbox escalation for macOS window services. Original sandbox process was terminated.
- Original user foliage scene, Android/WebGL/Vulkan GPU, full repository tests and CI NOT RUN. No push/CI URL.

Next: replace the undersampled static disk with a smooth deterministic spatial filter at bounded cost, inspect wide edges for banding and retain contact hardening. Record any penumbra support limit explicitly; rerun GPU captures, GLSL links and focused runner. Do not alter batching/default experimental flags or unrelated terrain sampler bindings.
