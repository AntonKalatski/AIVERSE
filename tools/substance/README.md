# Offline Substance baking — stage 1

Requires a separately installed/licensed Adobe Substance Automation Toolkit.
No Adobe binaries are bundled. Run from the project root:

```sh
python3 tools/substance/bake.py material.sbsar --cache /Volumes/KINGSTON/aiverse-substance --size 2048
```

Optional `--parameters values.json` accepts numeric exposed inputs, for example
`{"roughness": 0.5}`. `--renderer /path/to/sbsrender` selects the executable.
Defaults: 1K, two CPU cores, 1024 MB engine budget, ten-minute timeout.
The engine budget is a request to Substance, not an OS process memory limit.

Content-addressed output includes source bytes, renderer version, resolution and
parameters. Each entry contains PNG graph outputs and a hash manifest. Failed
jobs never publish a finished entry; cached outputs are checked before reuse.
Use one writer per cache key; concurrent publication may fail safely.

This is a standalone synchronous authoring tool, not Studio integration.
PNG is an intermediate, not GPU compression. Color-space assignment, normal
conventions, output semantic mapping, ORM packing, GPU compression, material
publication and background editor scheduling remain subsequent stages.
Actual SBSAR rendering has NOT been verified without Adobe SAT and fixtures.
