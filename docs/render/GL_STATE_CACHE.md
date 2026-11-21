# GL backend: redundant-state filter, native base vertex, unsynchronized uniforms

`engine/gfx/gl/GfxGL.cpp` (Windows, Linux, Android, Web/WebGL2, macOS GL tools)
issued the full pipeline state, texture bindings and vertex layout for every draw.
Pooled geometry (one vertex buffer, many meshes at different `baseVertex`) made it
re-point every vertex attribute per draw, and per-draw uniforms were written with
`glBufferSubData` into a 1 MB ring that the GPU was still reading.

## What changed

- **State cache** (`GLStateCache`): program, cull, depth test/write, color mask,
  polygon offset, blend, active texture unit, per-unit texture, element buffer,
  enabled attributes and the vertex layout are issued only when they change.
  The device is the only GL state owner in the engine; paths that change the same
  state outside draws invalidate it: `beginFrame`, `beginPass`, `beginRenderTargets`,
  `endPass`, `copyColor`, texture creation/upload/destruction, `bindTextureView`,
  buffer creation/update/destruction (GL reuses deleted names), shader creation and
  destruction.
- **Native base vertex**: `glDrawElements(Instanced)BaseVertex` on macOS GL, desktop
  GL 3.3 (Windows, via `wglGetProcAddress`), GLES 3.2 and `EXT`/`OES_draw_elements_base_vertex`
  (Linux/Android, via `eglGetProcAddress` after a version/extension check). WebGL2 and
  GLES 3.0 keep the attribute-offset emulation. `AIVERSE_GL_NATIVE_BASE_VERTEX=0` forces it.
- **Repeated uniform blocks** are not uploaded again within a frame (byte compare per slot).
- **Uniform ring writes** use `glMapBufferRange(WRITE | INVALIDATE_RANGE | UNSYNCHRONIZED)`:
  each write goes to a range unused in the current ring generation (the ring is orphaned
  per frame and when full). `AIVERSE_GL_UNIFORM_MAP=0` returns to `glBufferSubData`.
  WebGL2 and macOS GL keep their per-slot blocks (no mapping there).

## Measured (2026-11-21)

Test `gl_state_cache_gpu` (`tests/render/gl_state_cache_gpu.cpp`), real EGL/GLES 3.2,
Mesa llvmpipe 25.2 on x86_64, 20 000 draws alternating two pipelines with equal state,
one texture, alternating `baseVertex`; CPU submission time, best of 5:

| Case | Before | After |
|---|---:|---:|
| same uniform block every draw | 46.6 µs/draw | 0.54 µs/draw |
| different uniform block every draw | 48.8 µs/draw | 6.8 µs/draw |

llvmpipe is a CPU driver: these show the removed driver work, not a FPS promise for a
real GPU, ANGLE or a browser. Pixels of the 3×9 test frame (pipeline/texture/uniform
churn, a bound texture destroyed and recreated mid-frame, index buffer recreated,
`baseVertex` draws) are identical before and after, with and without native base vertex.

Not measured here: Windows GL drivers, Android GLES, WebGL2 in browsers.

```sh
cmake -S . -B build/linux -DAIVERSE_BUILD_TESTS=ON
cmake --build build/linux --target aiverse_gl_state_cache_gpu_tests
ctest --test-dir build/linux -R gl_state_cache_gpu --output-on-failure
AIVERSE_GL_BENCH=20000 AIVERSE_GL_BENCH_VARY=1 build/linux/aiverse_gl_state_cache_gpu_tests
```
