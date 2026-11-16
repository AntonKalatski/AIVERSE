---
name: aiverse-visual-qa
description: Check every visual change in AIVERSE on real screenshots - editor view_screenshot, headless Xvfb client captures, before/after comparison, phone device profiles, quality levels.
---

# Visual QA

Rule: any visual change is looked at before it is called done. Compile-only checks are not visual QA.

1. Editor/MCP: `view_focus` on the subject -> `view_screenshot`; `view_lighting` / `view_postfx` for variants; `ui_preview` for screens.
2. Headless Linux client:
   `Xvfb :97 -screen 0 1280x800x24 &` then
   `DISPLAY=:97 AIVERSE_SHADOWS=medium AIVERSE_SCREENSHOT=/tmp/shot.ppm AIVERSE_EXIT_AFTER_FRAMES=300 <build>/AiverseGame --map 0 --spectate`
   (`AIVERSE_FAST_FORWARD=seconds` for a mid-match frame, `AIVERSE_SPLASH=off`).
3. Before/after: the same camera, map, seed and frame count on both builds, then
   `python3 tools/visual/compare.py before.ppm after.ppm --diff diff.png [--threshold 16] [--max-ratio 0.001]` -
   changed pixel ratio, mean error, bounding box and a diff image (changes in red). Open the diff image and describe what changed in words.
4. Phones: `device_list` -> `device_set {device, orientation, safeArea: true}` -> `view_screenshot {window: game}` on a small phone, a tall phone and a tablet.
5. Quality levels: `AIVERSE_SHADOWS=off|low|medium|high`, `view_postfx {quality: low}` - weak devices must stay readable.
6. Open every screenshot yourself before reporting; attach the important ones. Real engine renders only, no mockups.

Code: `tools/visual/compare.py` (tests `tools/visual/test_compare.py`, PPM and 8-bit PNG, no packages). No library target.
