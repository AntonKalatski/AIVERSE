# AIVERSE Studio — MotionWeave attachment authoring (Stage 155)

The **MotionWeave — Attachments** window lives under the Studio **Look / Graphics** workspace,
is closed by default, and can be opened from the existing Modeling and Animation Graph toolbars
via their **Attachments** buttons. The window is deliberately independent of game genres.

## Author an attachment layout

Open a Studio project, then open **MotionWeave — Attachments**. Specify a project-relative
**Parent Model** file, for example `models/hero.glb`. Add one or more **Child Model**
files and **Socket** names, e.g. `models/lantern.glb` at `Hand.R`. Toggle **Enabled**
for each child or use **Detach** to remove an entry from the authored layout. Press
**Reimport & Publish** to validate and publish the complete source/child model set.

**Save JSON** persists the authored file layout independently of game rules; **Load JSON**
restores it. The path is relative to the project and is checked against parent-directory and
symlink escapes. The file supports the ModelView attachment fields plus the parent path:

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {"model": "models/lantern.glb", "socket": "Hand.R", "enabled": true},
    {"model": "models/backpack.glb", "socket": "Spine", "enabled": false}
  ]
}
```

This is a **file-backed authoring and socket validation window**, not yet a 3D attachment
viewport embedded in the Modeling/Animation Graph panels. Those panels keep their own
`AnimationGraphInstance` preview owner and their own directly-held Model; Studio does
not move or borrow those objects into this window's native ModelResourceCache. To test
actual child rendering, the standalone [ModelView attachment preview](modelview-attachments.md)
continues to use the same generic ImportedAttachmentAuthoring API.

## Model publication and reimport safety

Each **Reimport & Publish** builds a COMPLETE replacement resource session in a new,
dedicated `render::ModelAssetProvider` using the Studio GPU-device generation lease.
Both the parent and all enabled children are uploaded into the **same new cache**;
`AppearanceAssetRegistry` publishes their immutable metadata and
`ImportedAttachmentAuthoring::compile` checks every named socket against its real
parent rig. No existing live source resource is invalidated during staging. A missing
file, malformed model or unknown bone rejects the candidate and leaves the previously
published session active.

Publication deliberately uses a **full GPU idle barrier** before swapping the new
resource session and shutting down the old provider. This is a **blocking authoring**
operation, not zero-stall hot reload. Failure to prove GPU completion retains the
previous session; if old or scratch resources cannot be safely retired they are held
alive rather than destroyed without GPU proof. The project switch and final Editor
shutdown follow the same cache-owner lifetime rule.

A successful operation re-reads the bytes from disk, unlike ModelView's existing
**Rebuild** action (which only revalidates currently cached models). It does not
automatically replace a running gameplay client's assets, copy a Model into a
different owner, evaluate another animation graph or alter any simulation ECS data.
Snapshot pointers and compiled sockets do not own CPU/GPU Models; the provider does.

## Remaining work and verification

Stage 155 introduces the real Studio **asset-authoring integration**, including
file-backed add/toggle/detach/save and safely staged reimport. It does NOT yet
provide a 3D viewport in Studio for these attachments, nonblocking GPU asset
hot reload, editor drag/drop, attach-to-animated-child rigs, or runtime project
prefab/scene serialization. The existing Modeling and Animation Graph windows
only open this dedicated window; they do not share its ModelResourceCache.

**NOT RUN** for this stage: C++ compilation, CMake/CTest, real Metal/Vulkan/GLES
rendering, UI integration, device-fence fault injection, sanitizer or performance
benchmarks. Continue from [Stage 155 agent handoff](agent-handoffs/animation-stage155-studio-attachment-staged-reimport-2026-11-20.md).
