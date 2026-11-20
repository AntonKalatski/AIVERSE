# AIVERSE Texture RHI — reproducible Linux GLES3 build / pixel acceptance gate
Date: 2026-11-20

## Branch, files and commits
- Branch: `feature/texture-rhi-unification`; do not merge to main.
- Source modification: `.github/workflows/texture-rhi.yml` commit `58c4ee8a22035069f01960c2c3e90049a59da0d7`.
- No production shader, RHI or fixture logic changed in this stage.
- Existing native fixture: `tests/render/gles_texture_array_gpu.cpp` (earlier work on this branch).

## Architecture and verification
Existing GitHub Action already includes Linux portable texture focused matrix OpenEXR=OFF/ON and Vulkan build/source tests. This stage adds independent opt-in `gles-gpu` job, triggered by pushes to `feature/texture-rhi-unification` or relevant PR updates, using an Ubuntu 24.04 GitHub Actions runner with Mesa software GLES3, EGL surfaceless, and G++/CMake/Ninja.

The job configures the actual AIVERSE CMake project with `AIVERSE_TEXTURE_GLES_GPU_TESTS=ON`, builds `aiverse_gles_texture_array_gpu_test`, then runs `ctest --verbose --no-tests=error -R '^gles_texture_array_gpu$'` with a mandatory suite-specific success marker and a FAIL scan. It uploads the CTest stdout as an artifact for evidence. The fixture exercises the production `gfx::createGLDevice(Backend::GLES3)`, array mip/layer uploads, padded rows, legacy Texture2D, GPU asynchronous FBO/PBO readback and preservation of all dirty unpack states. This is an actual GL driver when the runner executes, but Mesa llvmpipe is **software GPU rendering, not physical GPU parity**.

## What was *actually* executed this turn
- Container tool availability probe: `cmake`, `clang++`, Node and Python are installed. `emcc`, `glslangValidator`, EGL/GLES development headers are not installed; no AIVERSE checkout exists in container. `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed with DNS resolution error.
- GitHub connector inspected `.github/workflows/texture-rhi.yml` and the existing Mac Studio final verify script.
- GitHub commit of the new CI job succeeded.
- GitHub commit status and pull-request-associated workflow run query for new SHA returned empty arrays. **No successful CI run or test result has been confirmed.** Never report compile PASS or GPU PASS without logs and SHA.

## Exact next agent actions
1. Check [the Texture RHI workflow](https://github.com/chromealex/AIVERSE/actions/workflows/texture-rhi.yml) for run associated with commit `58c4ee8a` or a subsequent branch SHA. If no run, inspect repository Actions permissions/path filters and trigger the workflow manually, or use an accessible runner. Do not assume push triggered a run.
2. Fetch Linux GLES3 job logs and correct compiler issues or failing GL pixels in the same branch; preserve the exact tested SHA and log artifact.
3. Run Emscripten WebGL2 standalone acceptance + Playwright browser `--vt`, and full Mac Studio `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` for actual Metal GPU parity. Check Vulkan source and GPU gating separately.
4. Only merge when required gates have genuine PASS evidence. Keep PR #21 Draft and experimental GPU-authored ICB default OFF.

## Limitations and handoff rule
The new job is an **unverified CI definition**; GitHub Actions availability, repository permissions, checkout submodules, CMake target build, driver support and marker match are unknown. For every further implementation stage, create a separate document with architecture/API, changed paths, branch+commit, *actual* test results, limitations and next action.
