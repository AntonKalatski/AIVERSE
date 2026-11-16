---
name: aiverse-cables
description: Generate AIVERSE procedural cables - power lines, ropes, wire bundles with sag, metric UVs and endpoint attachment - with the CPU geometry module `AIVERSE::cables` or the modeling MCP document, and export them as models.
---

# Procedural cables

Use for static or rarely changing cable geometry. Simulated ropes in gameplay are
`skills/gameplay/aiverse-physical-interactions`; cloth is `skills/visuals/aiverse-cloth`.

Authority: `docs/cables.md`. Code: `engine/cables/Cables.h` - CPU only, no GPU, physics, editor, network or
simulation dependency; units are metres, coordinates local to the resulting model.

1. Generate on creation or when an anchor moves, cache the result, upload through the normal model path.
2. Authoring: modeling documents (`modeling_create`, `modeling_edit`, `modeling_export`; see
   `skills/visuals/aiverse-modeling`); example `examples/cables/power_lines.model.json`.
3. Check: `aiverse_cables_tests` (attachment, sag, UVs, bundles, cancellation); `aiverse_modeling_tests` for the
   document adapter; a screenshot of the exported model.

Workflow recipe over an existing engine module; no new library.
