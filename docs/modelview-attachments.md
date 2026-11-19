# ModelView — real imported socket attachments

This is an **opt-in native model asset preview and authoring workflow** that exercises the shared, genre-independent
`engine/view/ImportedAttachmentAuthoring.h` API. The parent and imported static child models are owned by the same
`render::ModelAssetProvider`; `MotionWeave`/View never interprets a game genre or action.

## Start with real models

```sh
# The parent file must contain the chosen named socket/node.
aiverse_modelview models/actor.glb@Walk \
  --attach models/lantern.glb@Hand.R \
  --attachments-file assets/actor.attachments.json
```

Or start with an existing JSON file:

```sh
aiverse_modelview models/actor.glb --attachments-file assets/actor.attachments.json
```

The JSON is intentionally lightweight, authored by ModelView and independent of any gameplay config:

```json
{
  "attachments": [
    {"model": "models/lantern.glb", "socket": "Hand.R", "enabled": true},
    {"model": "models/backpack.glb", "socket": "Spine", "enabled": false}
  ]
}
```

`--attach child.glb@SocketName` adds extra entries to those loaded from the JSON file.
Attachment preview automatically activates native provider mode and disables primitive merging/baked display.
`--game` gallery mode cannot be combined with this feature; ordinary standalone model previews and gallery
behavior are otherwise unchanged. Multiple parent input models are allowed, but every active named socket
must exist on **each** parent or an edited layout is rejected rather than partially published.

## ModelView UI

The **Attachments** window can toggle an authored child, **Detach** it from the layout,
add a child by specifying its *real imported model file* and named socket,
**Rebuild** the current layout, and **Save** the configuration.
If no `--attachments-file` was specified, Save defaults to
`<first-parent-model-path>.attachments.json`.
Changes apply at the next **GPU/device owner frame boundary**, not inside the immediate draw submission.
An invalid edit keeps the previous compiled attachment layout active and reports an error in ModelView.

The two operations are distinct:
- **Detach** removes an attachment from the scene layout, but does not immediately unload its cache-owned Model
  or evict GPU resources; the provider retains shared residency until safe later retirement/shutdown.
- **Rebuild** recompiles the layout using the provider's **currently resident** model revisions; it is **not**
  an on-disk file hot-reimport. A changed child file still needs a model-source invalidation/republication
  transaction after existing graphics submissions finish. This workflow does not yet expose that transaction.

## Source and lifetime contracts

The parent Animator runs exactly once in ModelView, then the genre-neutral
`ImportedAttachmentAuthoring::prepare(..., AnimationOwner::HostPrecomputed)`
builds one borrowed `View::Object` containing current parent pose/matrices and compiled child sockets.
`View::submitOne` renders parent and children without ticking another animation graph.

The compiled layout holds published immutable Appearance metadata and precompiled socket indices,
**not** permanent ModelReadLeases. Each drawn instance receives a move-only
`ImportedAttachmentAuthoring::Prepared` that owns short-lived CPU read leases for source
and imported child Models (plus optional visual LODs), released after synchronous View submission.
GPU resource retirement still belongs to ModelAssetProvider / ModelResourceCache and explicit
device completion tokens. No high-crowd performance or native correctness claim should be made
until the full test pass.

This Stage 154 functionality is **source implemented only**. CMake build, CTest, actual render,
Metal/Vulkan/GLES, GPU fence failure injection, sanitizer and perf verification have not been run.
Continue from [Stage 154 handoff](agent-handoffs/animation-stage154-modelview-attachment-workflow-2026-11-19.md).
