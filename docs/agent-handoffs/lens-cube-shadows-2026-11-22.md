# LENS: shared sun shadow map for cube captures (2026-11-22)

Branch `feature/aiv20261122_lens_cube_shadows` (from main `8daa28bad`).

## Problem
The site panorama (`p3-cube`) showed floor and wall lighting that changed abruptly at
the cube-face seams. Each of the six face cameras ran `Renderer3D::beginScene`, which fits
the orthographic sun frustum to that camera (`computeShadowMatrix`); faces looking up or
sideways got different light boxes and texel sizes, so the same wall or floor point was
shadowed differently in neighbouring faces.

## Change
- `CameraCaptureView`: appended `shadowBounds`, `shadowMin`, `shadowMax` (aggregate order
  kept; stereo/XR views leave them off).
- `makeCubemapCameraViews(source, keys, faceSize, hdr=false, shadowRadius=40.f)`: every
  face gets the same box `eye ± shadowRadius`; `0` or a non-finite radius keeps per-face fit.
- `Renderer3D::shadowBoundsState()/restoreShadowBounds()`: scoped override of the existing
  `setShadowBounds`.
- `CameraRenderPipeline::renderCaptures`: applies a view's box before `beginScene` and
  restores the previous bounds after `render`.
- `tools/site/lens_showcase.cpp`: cube layout uses the box; `LENS_CUBE_SHADOW=radius`
  (0 = old behaviour) for comparisons.
- `tests/render/camera_capture_views.cpp`: shared box on all faces, radius 0 disables.

## Verification (Mac, Metal, KINGSTON build trees)
- `aiverse_lens_showcase`, `aiverse_client`, and tests `camera_capture_views`,
  `camera_client_adapter`, `camera_sprite_vfx_prepared_api` built; the 3 tests passed.
- Cube at eye (0,1.6,0.3): with `LENS_CUBE_SHADOW=0` the gallery wall under the banners
  changes tone across the +Z/−X seam; with the shared box it is continuous. No change in
  faces that were already consistent. pipelineFailures=0.
- Not covered: Vulkan/GL, full CTest. A 40 m box at High (2048) is ~4 cm per shadow
  texel; larger worlds may pass a smaller radius or set bounds in the host.

## Notes
- The thin dark curve on the floor in the panorama is the step of the arcade floor, not a seam:
  it stays with the sun off and at 1024-px faces.
