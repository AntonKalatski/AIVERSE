# SpectraFX Stage 116 — full native acceptance 128/128, Editor regression resolved

Date: 2026-11-20
Validated source / feature branch SHA: `ba4b126fd533cdbcb8443f46a68c6d8afe76867f`
Platform: Apple M3 Ultra, Darwin
This is **documentation-only** evidence for pre-merge SpectraFX; it does not certify the later integration branch.

## Actual native acceptance from user report

| Stage | Verified |
|---|---|
| configure | exit 0 |
| spectrafx_core_build | exit 0 |
| architecture | exit 0 |
| host_integration_build | exit 0 |
| full_build | exit 0 |
| ctest_full | **128/128 PASS**, 885.29 s |
| ctest_spectrafx | **13/13 PASS**, 6.00 s |
| ctest_metal | **4/4 PASS**, 5.13 s |
| metal_evidence | **4/4 real PASS**, skipped 0, missing 0, fail/unknown 0 |
| visual_review | **5/5 PASS** |

Failed stages: **0**; skipped stages: **0**; 10/10 acceptance stages exit 0. The previous Editor `dt` lexical-scope build regression was resolved and the full binary builds again. The focused `spectrafx_preview_policy` and `spectrafx_retirement` CTests passed.

## Metal and visual details

Real RHI on M3 Ultra: `gpu_frame_timing requested=512 scheduled=512 valid=512 invalid=0 p50_ms=0.0800 p95_ms=0.3958`. Nominal particle data peak 36 KiB, Metal device-wide allocation 1472 / peak 1728 / after 1664 KiB; 512 frames x 256 particle slots, `retire_remaining=0 gpu_healthy=1`. The allocation counter is **device-wide**, not resident VRAM or a leak verdict.

Two camera alpha order first indices encoded 51/229. Deferred/stale impact feedback: queued 1, discarded 1, delivered 0. Renderer3D scene A/B: changed-pixel count 168/304, centroids 63.50/43.07, `depth_center_delta=0`. PRISMA HDR/Bloom active with ten passes and 232 changed scene pixels; zero failed checks. Visual review from seven authentic PPMs: changed A/B/PRISMA pixels 192/322/232; depth, reset, projection and PRISMA objective invariants all PASS.

## Scope limits

Native source verified at this exact **pre-main-merge** SHA only. Full CTest does not execute live ImGui mouse orbit/zoom/restart/pause/step UI interactions, real gameplay frame pacing or production Vulkan/GLES cross-backend tests. No claim of leak-free behavior from a single device-wide memory reading. All of these still require separate QA.
