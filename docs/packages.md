# Packages: optional content from the catalog

The engine repository carries code and a minimal set of assets. Heavy optional content — PBR
material libraries, their source images, project templates, model packs, sound, effects — lives in
the package catalog [AIVERSE-PLUGINS](https://github.com/chromealex/AIVERSE-PLUGINS). A project
downloads only what it uses.

| Piece | Where |
|---|---|
| Catalog: packages by category (`materials/`, `textures/`, `templates/`, `models/`, `audio/`, `vfx/`, `ui/`, `scripts/`, `plugins/`, `tools/`) | AIVERSE-PLUGINS, `<category>/<id>/aiverse.package.json` |
| Archives `<id>-<version>.zip` | GitHub releases `<id>@<version>` of AIVERSE-PLUGINS |
| Index (everything the engine and the website read) | `index.json` on its `main`, written by its `publish` workflow |
| Client | `tools/pkg/aiverse_pkg.py` (stdlib Python; `--json` on every command) |
| Cache on this machine | `~/.aiverse/packages/<id>/<version>/` (`AIVERSE_PACKAGES_HOME`), downloads verified by SHA-256 |
| Website | page **Catalog** (`catalog.html`): previews, description, minimum engine version, download |

## Using packages

```sh
python3 tools/pkg/aiverse_pkg.py search terrain              # also --category materials, --kind content
python3 tools/pkg/aiverse_pkg.py info terrain-pbr
python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py status --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py install --project ~/Games/MyGame   # fresh clone: restore from aiverse.lock
python3 tools/pkg/aiverse_pkg.py verify --project ~/Games/MyGame    # exit 1 when installed files changed
python3 tools/pkg/aiverse_pkg.py remove terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr-sources   # authoring sources: prints the cached folder
python3 tools/pkg/aiverse_pkg.py create <template-id> ~/Games/New    # new project from a template package
```

- **`content`** packages are copied into the project at `install.mount` (for `terrain-pbr`:
  `materials/terrain`); `install.mergeJson` merges their material library into the project's
  `materials.json`. Ids the project already uses with another value are never overwritten
  (`add` fails; `--force` overrides). The project ships the files like any of its own, so a build
  needs no network and `aiversebuild` packs only what the project holds.
- `game.json` → `"packages": {"terrain-pbr": "^1.0.0"}` lists the wanted ranges; `aiverse.lock` next
  to it pins the exact version, archive hash and every installed file's hash. Commit both.
- **`source`** packages (bake inputs) and other tool data are never copied into projects: tools call
  `aiverse_pkg.package_dir('<id>')` (Python) and get the cached folder, downloading it once.
- **`template`** packages are whole project folders: `create` copies one and installs the packages
  its `game.json` lists.
- Version ranges: exact `1.2.3`, `^1.2` (same major), `~1.2.3` (same minor), `>=1.0.0`, `*`.
  Every package declares `minEngine`; `add` prints a note when this checkout is older.
- Offline: `AIVERSE_PACKAGES_OFFLINE=1` uses only the cache and the last downloaded index.
- C++ host tools and the editor find cached packages with `engine/pack/Packages.h`
  (`aiverse::pack::cachedPackageDir("vegetation-pbr")`). A game never reads the cache.

## Web viewer

`apps/catalogview` (CMake `-DAIVERSE_BUILD_CATALOGVIEW=ON`, target `aiverse_catalogview`) shows a
package's materials or its glTF / GLB models through Renderer3D with an orbit camera. A package opts in
with a `viewer` block in its manifest; the catalog's `pages` workflow builds `view/<id>/<version>/`
(view.json + the files exactly as published, textures at their own size) and the WebAssembly viewer
from this repository, and deploys both to the catalog's GitHub Pages. The website's Catalog card gets
a **3D preview** button that opens `viewer/?src=../view/<id>/<version>&lang=ru|en` in the card.

- **Materials:** a shape switch (sphere, cube, cylinder, plane) over a tiled slab; texel density is the
  same on every shape (one repeat per `repeat` metres, whole repeats around closed shapes so the seams
  meet). The info panel lists every texture map with its size in pixels and its file size.
- **Models:** Auto LOD (the level the renderer picked is highlighted), a fixed LOD 0…n, or STRATA
  (clusters built from the model on first request; *Clusters* colours them). *Wireframe* shows the
  edges of the shown level. The panel lists triangles per level, whether the engine generated the
  LODs, STRATA cluster count and build time, and the model's textures.
- Every loading state shows the animated AIVERSE mark (the same one as the site, Studio and web
  builds; `tools/branding/logo/loader.py` keeps the copies in sync, CTest `branding_loader`).

URL parameters: `src`, `item`, `shape=sphere|cube|cylinder|plane`, `lod=auto|strata|N`, `wire`,
`clusters`, `lang`. Desktop check: `aiverse_catalogview --src <view folder> [--item N] [--shape S]
[--lod auto|strata|N] [--wire] [--clusters] [--yaw deg] [--pitch deg]` (`AIVERSE_SCREENSHOT` works as
in the client; every change prints a `catalogview-info: {...}` line, the same JSON the page receives).

## Packages that left the engine repository (2026-11-22)

| Package | Was | Now in the engine |
|---|---|---|
| `terrain-pbr` (materials, 14 PBR ground materials) | `engine/assets/materials/terrain` | checkerboard only (empty projects) |
| `vegetation-pbr` (materials, bark + leaf atlases, bark projection, winter) | `engine/assets/materials/vegetation` | — |
| `terrain-pbr-sources` (textures) | `tools/terrain/pbr_sources` | — |
| `vegetation-pbr-sources` (textures) | `tools/vegetation/pbr_sources` | — |

The tools that used those folders now take them from the catalog: `tools/terrain/install_pbr_pack.py`
(= `add terrain-pbr`), `bake_pbr_pack.py --root <payload copy>` (sources from the catalog),
`build_detail_materials.py`, `bake_landscape_pbr.py`, `tools/vegetation/*`. The editor's TerraWeave
forest preview reads `vegetation-pbr` textures from the project, else from the cache.

## Skills name their packages

A repository skill lists the packages it needs in its frontmatter:

```
---
name: aiverse-terra-vegetation
description: ...
packages: vegetation-pbr, terrain-pbr
---
```

`python3 tools/skills/catalog.py search|plan` shows them (`contentPackages`, with ready install
commands in `plan`), and `python3 tools/pkg/aiverse_pkg.py skill <skill-id> --project P` installs
them. Agents: before following such a skill in a project, install its packages; never copy package
files into the engine repository.

## Adding or updating a package

Pull request to AIVERSE-PLUGINS (its `CONTRIBUTING.md`); the owner reviews and merges. Its CI checks
the manifest, previews, licence and payload; after the merge `publish` releases the version and
rewrites `index.json`, which the engine and the website read directly. Small payloads live in the
package's `files/`; big ones are an https `archive` or a `source` folder of a public GitHub
repository at a fixed commit. A published version is immutable: changes need a new `version`.
Heavy assets are never committed to the engine repository again.
