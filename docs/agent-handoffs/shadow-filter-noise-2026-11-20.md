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

## Stage 2: smooth non-temporal penumbra (2026-11-20)

Code: `d43cd1eae3586f171571c377306e4cfdecbdbfc9` on `feature/studio-html5`.
Paths: `engine/render/Shaders.h`, `tests/render/temporal-pcss.md`.
The non-temporal path now uses a deterministic golden-angle disk: 64 taps High, evenly distributed 32-tap subset Medium, normalized squared radial taper. GLSL/MSL match. PCSS radius/blocker search/receiver-plane correction remain; Low and the original temporal disk remain unchanged. No API, uniform layout, save or batching changes.

A texel-dense tent prototype was tested and rejected (not committed): its support cap removed bands but reduced near/middle/far transition widths to 3/4/3 and failed the contact-hardening fixture. Do not resurrect that cap as a fix. The final tapered disk preserves the radius, though its weights produce narrower effective transitions than the old equal-weight disk.

Executed validation on the final source tree (commit performed after testing):
- `python3 tests/render/check_model_lod_shaders.py`: PASS 32 desktop GLSL/GLES variants.
- `cmake --build /private/tmp/aiverse-shadow-noise-20261120 --target aiverse_hdr_scene aiverse_hdr_gl -j 6`: PASS. Log `disk-build.log`; no compiler warning/error matches.
- Metal high/fixed/medium/low/low-fixed and desktop GL high: all six capture processes exit 0. Final images under `disk-captures/`, logs `disk-*.log` in the isolated build.
- `python3 tests/render/verify_contact_shadows.py /private/tmp/aiverse-shadow-noise-20261120/disk-captures`: PASS; Metal 3/6/12, GL 3/7/12, Medium 3/7/12; fixed 18/17/17. Low/fixed remain identical.
- `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter '^hdr_policy$'`: PASS 1/1, report `test-reports/20261120-134822-7875/summary.txt`.
- Viewed full final Metal contact-new.png and enlarged shadow-edge-zoom.png. Pronounced repeated sample bands from Stage 1 are substantially reduced; arbitrary foliage is not verified artifact-free.
- `git diff --check`: PASS. No push or CI dispatched; CI URL/status: N/A.
- Stage 1 TAA fixture terrain-sampler compile failure remains unresolved, not a successful temporal validation. Temporal shader branch is unchanged by Stage 2.
- Original foliage scene, full editor build, full suite, Vulkan/Android/WebGL GPU and GPU performance: NOT RUN.

Risk: non-temporal Medium/High filtering grows from 12/24 to 32/64 bilinear comparisons; thin alpha foliage and extreme softness may still alias. Low cost is unchanged. The user editor binary has NOT been rebuilt/replaced.
Next: rebuild the user's editor on this branch, compare the original tree scene stationary and moving with HDR/TAA off/on, measure target GPU cost; address the separate 17-sampler terrain TAA fixture blocker before claiming temporal end-to-end coverage.
