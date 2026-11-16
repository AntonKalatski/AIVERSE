---
name: aiverse-paint-layers
description: Draw persistent AIVERSE render-texture canvases from view Lua - trails, ground markings, accumulated masks, territory-style paint - with `view.render_texture` and `view.paint_layer`, presentation only.
---

# Render textures and Lua paint layers

Use for visuals that accumulate over time on the ground. They exist in **view scripts only** and never change
simulation, ownership, checksums or victory; gameplay per-cell state belongs in grid fields (`docs/fields.md`).

Authority: `docs/render-texture.md`; view Lua in general: `docs/ui-scripting.md`.

1. `view.render_texture{width, height}` makes an offscreen canvas; `canvas:stroke(x0, z0, x1, z1, {radius,
   softness})` paints capsules (a zero-length stroke is a circle).
2. `view.paint_layer{texture | resolution, bounds, color, opacity, border, lift}` shows a canvas over a terrain
   rectangle (world x/z); `copy_from`, `clear`, `destroy` manage it.
3. A territory source can drive the same brushes (see the doc's "Territory source").
4. Respect the limits: 16 live canvas / layer objects per VM, 8 million requested texels, bounded queued strokes.
5. Check with `view_screenshot` on GL and Metal; nothing in `--verify-restore` output may change.

Workflow recipe over existing engine code; no runtime library.
