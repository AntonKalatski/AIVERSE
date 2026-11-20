# AIVERSE textures — WebGL2 context loss and restoration
Date: 2026-11-19

## Branch and implementation
- Branch: `feature/texture-rhi-unification`, main unchanged.
- Implementation: `edb22101a9ba187dc7780ebf7f2e69d16d391224`
- Modified `engine/platform/web/PlatformWeb.cpp` (generic web graphics lifecycle, not genre-specific).
- Public platform `AppCallbacks` API unchanged: `init(Device&)`, `frame(Device&, dt)`, `shutdown()`.

## What changed
Existing WebGL2 initialization used Emscripten to create the canvas GL context but did not register context lost/restored event callbacks. With a lost context the main loop continued rendering with invalid GPU texture and framebuffer handles.

Now platform registers `emscripten_set_webglcontextlost_callback` and `emscripten_set_webglcontextrestored_callback` on the canvas. When lost it sets `g_contextLost` and pauses frame rendering; callback returns `EM_TRUE` to allow browser restoration. When restored, a flag schedules recovery on the main loop (not the browser event callback). Recovery makes the EGL/WebGL context current; calls the application's `shutdown()` if initialized; releases old `gfx::Device`; constructs new `createGLDevice(Backend::GLES3)`; invokes the existing `init(Device&)` callback so application code can upload textures/shaders/RTs from asset sources; then resumes future frames. The first restored iteration intentionally does not render. Failed make-current/device creation leaves rendering suspended.

## Important ownership contract
The application `shutdown()` must release all GPU objects and streaming coordinators associated with the old gfx::Device; `init()` must recreate every texture, virtual texture atlas/page table, pipeline, FBO and any cached handles. Host-side immutable asset bytes / disk cache must survive reinitialization or be reloaded. The platform cannot automatically restore user-owned textures from arbitrary GPU-only data without those callbacks. Browser restores context only, **not** texture contents; this path performs a whole Device reinitialize, not per-texture resurrection.

## Validation performed
GitHub update succeeded and source was inspected. **No Emscripten compiler build, browser WebGL2 context-loss extension test, automated runtime GPU readback, CTest or native GPU test has run** after this code change. Thus neither callback signature portability nor application shader/texture re-creation has been proven.

## Next steps
1. Compile Emscripten/WebGL2 build. Confirm Emscripten version accepts the context event callbacks and no compile/link errors.
2. Add browser-driven `WEBGL_lose_context` test, invoke context loss/restore during active virtual-texture streaming and validate suspension and post-restore new GPU handle contents. Cover failure/partial restore and repeated cycles.
3. Verify `shutdown()` is safe when context is lost and does not dereference invalid GPU resources, including active streaming workers and callback queues.
4. Complete native GLES3 texture-array acceptance (`gles_texture_array_gpu`), actual WebGL2 RGBA readback and the Metal/Vulkan cross-frame atlas tests.
5. After all source work run Mac Studio `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`; separately execute browser and Linux GLES3 tests. PR #21 remains Draft and GPU-authored ICB remains default OFF.

After each stage update handoff with changed files/APIs, actual verification evidence, limitations, commit and next step.
