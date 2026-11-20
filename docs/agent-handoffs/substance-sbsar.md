# Substance SBSAR handoff

## 2026-11-21 — stage 1: standalone offline bake tool

Branch: `feature/studio-html5`. Implementation: `fc88afd2c`.
Starting HEAD: `1f3d53fcb0b6e7470fdf5d58e0afb62cabe3ba5a`.
Paths: `tools/substance/bake.py`, `tools/substance/README.md`.

Added an external SAT subprocess wrapper with numeric exposed parameters,
512–4096 output size, requested engine RAM/CPU budgets, timeout, SHA256-keyed
cache and staging publication. Cache outputs carry integrity hashes.
No runtime API, save/ECS format or renderer changes. Existing unrelated dirty
material/editor files were preserved. No Studio controls or PBR mapping yet.

Executed: `python3 tools/substance/bake.py --help` — PASS, CLI displays.
Executed: `python3 tools/substance/bake.py AGENTS.md --cache /tmp/aiverse-substance-check`
— expected exit 1 with explicit missing-sbsrender diagnostic, before rendering.
Real SBSAR bake/cache reuse/failure recovery/GPU/performance: NOT RUN because
Adobe SAT and SBSAR fixtures are unavailable. CI: not triggered, no CI URL.
First ordinary commit attempt failed due sandbox `.git/index.lock`; retry with
authorized escalation succeeded. This is not a full material import stage.

Risks: Adobe CLI/version compatibility unverified; memory budget is not an OS
limit; concurrent writers to the same key can fail publication safely; PNG
outputs are not yet checked for decodability or mapped by semantic usage.
Next: obtain installed SAT plus representative licensed SBSAR fixtures, verify
real CLI and cache behavior, then implement explicit semantic mapping,
normal/color-space handling, ORM packing and existing asset-pipeline compression.
Only after that wire asynchronous Studio import and preview using shared UI.

## 2026-11-21 — stage 2: explicit PBR bundle packing

Branch `feature/studio-html5`; code `796db270c`, starting HEAD `19b27693b`.
Paths: tools/substance/{pack.py,test_pack.py,requirements.txt,README.md}.
Added explicit output semantic mapping, grayscale ORM packing R=AO/G=roughness/
B=metallic, default channels 1/1/0, optional DirectX normal green flip, color-space
metadata and bundle-relative Studio material field object. Same-size 8-bit PNG
inputs only, <=4096; staging avoids publication of partial bundles. No guessing
authored output names, library writes, editor UI, GPU compression or ECS changes.
Existing unrelated dirty material/renderer work preserved.

Verification executed:
- Initial system Python Pillow import FAILED (dependency absent).
- Initial temporary venv pip install FAILED due sandbox DNS; escalated install
  succeeded, Pillow 12.3.0 installed in /tmp/aiverse-substance-venv only.
- `PYTHONDONTWRITEBYTECODE=1 /tmp/aiverse-substance-venv/bin/python tools/substance/test_pack.py`
  PASS, 2 tests: exact channel/default/normal pixels and mismatched-size rejection
  with no published output.
Real SAT/SBSAR generation, native loading, GPU compression/performance, Studio
preview: NOT RUN; SAT/fixtures missing and editor not integrated. CI not run, no URL.

Compatibility: generated material.json is a field object, not the complete
materials.json library; paths require rebasing on project import. bundle.json
schema 1 records intended color spaces; no existing loader consumes this metadata.
Risks: only scalar numeric bake params and 8-bit maps; no opacity inference,
16-bit height/displacement or automatic normal-convention detection.
Next actionable step: validate with real SAT plus a licensed SBSAR fixture,
then connect bundle path rebasing/publication and existing texture compression
through native asset authoring before adding shared-UI background preview.
