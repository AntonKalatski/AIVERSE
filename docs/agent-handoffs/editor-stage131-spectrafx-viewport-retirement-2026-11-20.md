# AIVERSE — Stage 131: SpectraFX windows now pass Viewport GPU retirement owner

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Isolated hotfix branch:** `fix/spectrafx-preview-viewport-retirement-20261120`
**Base:** current `main` at branch creation
**Related worktree:** `~/Projects/AIVERSE-terraweave-stage70-local-qa`
**Changes:** two editor window source files, this handoff. No renderer/backend or signature downgrade.

## User-reported native failure

The owner's local TerraWeave **full build** reached 95% then failed compiling `apps/editor/windows/SpectraFxGraphWindow.cpp` and `SpectraFxGpuAssetsWindow.cpp` with:

```
error: too few arguments to function call, expected at least 4, have 3
previewViewport_.ensure(*device_,int(width*dpi),int(height*dpi));
previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi));
apps/editor/Editor.h:91:
void ensure(gfx::Device& dev, int pw, int ph, ViewportGpuRetirement& owner,
            const char* debugName = "Viewport");
```

This is an API mismatch: the editor `Viewport` now needs its GPU retirement queue owner for safe deferred texture destruction, and on the new editor lifetime design a device lease is also required before the viewport references the GPU. Reducing the `ensure` signature back to three arguments or creating a temporary local retirement queue would break GPU lifecycle guarantees.

The previous targeted `spectrafx_metal_rhi` CTest passed 1/1, but it does not compile every editor window and therefore did not catch these two calls. Compiler warnings in `SpectraGraphWindow.cpp` about unused parameter/function are not the cause of this failed build.

## Fix implemented in isolated branch

**Commit `a0e721ca98a6c215394f9e5e3b8d523d5b8b7732`:** `apps/editor/windows/SpectraFxGraphWindow.cpp`:

```cpp
previewViewport_.setGpuDeviceLease(ed.gpuDeviceLease());
previewViewport_.ensure(*device_,int(width*dpi),int(height*dpi),
                        ed.viewportRetirement(),"SpectraFX Graph");
```

**Commit `91952387a9db2976777986001db2a414b893093b`:** `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`:

```cpp
previewViewport_.setGpuDeviceLease(ed.gpuDeviceLease());
previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi),
                        ed.viewportRetirement(),"SpectraFX GPU Assets");
```

Both patched files now have **identical GitHub blob SHAs** to the already-correct implementation in `integration/terraweave-physis-stage70-20261120`. This confirms the two changes align with the existing integrated design instead of introducing a novel alternative.

- In `Editor.h`, `gpuDeviceLease()` returns a lease from the editor's `RenderDeviceLifetime` and `viewportRetirement()` returns the editor-owned `ViewportGpuRetirement&`.
- `Viewport::ensure` requires that owner for managed retirement of resized/replaced color/depth render targets. The window's `previewViewport_.release(...)` continues to use existing shutdown logic.
- No changes to viewport implementation, GPU resource lifetime, Metal/Vulkan/GLES RHI, engine rendering paths, or external window APIs.

## Validation status (truthful)

**Performed:** inspected the exact current remote editor `ensure` signature and two old call sites; inspected matching valid fix in the TerraWeave Stage70 integration branch; committed the two focused edits to an isolated branch; fetched both modified files again and compared their entire GitHub blob SHA to Stage70's known-consistent files. **Not performed:** actual local Mac C++ compile with user's worktree content, full editor build, full CTest, runtime editor preview GPU lifetime smoke test, Vulkan/GLES builds. No claim of build success until owner verifies it.

## Apply locally preserving other agents' files (no hard reset)

First inspect the actual local sites:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
rg -n -C 3 'previewViewport_\.ensure|previewViewport_\.setGpuDeviceLease' \
  apps/editor/windows/SpectraFxGraphWindow.cpp \
  apps/editor/windows/SpectraFxGpuAssetsWindow.cpp
```

If these still show the old 3-argument calls and `Editor.h` contains the matching 4-argument signature, change **only** these calls to the above snippets. Do not overwrite the entire two files or remerge all of main into the in-progress worktree. To use isolated Git commits cautiously, fetch and inspect the two tiny diffs first:

```bash
git fetch origin fix/spectrafx-preview-viewport-retirement-20261120
git show --format= a0e721ca98a6c215394f9e5e3b8d523d5b8b7732 -- apps/editor/windows/SpectraFxGraphWindow.cpp
git show --format= 91952387a9db2976777986001db2a414b893093b -- apps/editor/windows/SpectraFxGpuAssetsWindow.cpp
```

`git apply --check` may fail on a local worktree whose other agents have altered surrounding context. When it fails, edit only the named call sites rather than force-apply.

Verify:

```bash
git diff --check
cmake --build build/terraweave-main-acceptance --target aiverse_editor --parallel 8
# If target succeeds, finish full build and then CTest:
cmake --build build/terraweave-main-acceptance --parallel 8
ctest --test-dir build/terraweave-main-acceptance --output-on-failure --parallel 8
```

## Next agent

Review actual native compiler/test results from local Mac Studio. If additional `Viewport::ensure` call sites fail, migrate them with their real editor retirement and GPU lease owners; do not create temporary short-lived `ViewportGpuRetirement`, do not revert the required fourth argument, and do not rewrite the whole editor. Keep the CPU MDI benchmarking PR #39 separate and Draft; it is unrelated to this editor hotfix.
