# Luma native direct lighting

`engine/render/LumaWeavePostProcess.h` explicitly wraps the existing PRISMA
`PostProcess`. It adds linear HDR direct illumination before the downstream
resolve/tonemap. It does not own indirect GI or authoritative ECS state.

Initialize on the renderer thread, publish a `LumaWeaveRegistry` and matching
`LumaWeaveClusters`, and publish validated `GiGpuBvh` geometry with a nonzero
revision. Publish only outside encoders; retry while compute is pending. Enable
explicitly and pass the wrapper to `Renderer3D::render`. The normal
`resolvePrisma()` call finishes the wrapper and downstream processing.

Metal only. Up to 4096 stable point/spot sources, 8192 shadow triangles; clustered
overflow scans all sources rather than dropping energy. A bounded policy reduces
sample count/resolution; it can reject an excessive workload. The caller must
report unsupported/rejected configurations and retain ownership of its fallback.
Never silently truncate shadow geometry or replace a rejected source publication
with a claim that old GPU data is current.

Receiver side passes capture actual opaque PBR material, and High/Ultra capture
world normals/roughness (including graph and normal-map evaluation). Balanced
uses geometric normals and diffuse Lambert reflectance. High/Ultra use GGX.
Custom geometry needs both `drawCustomMaterialReceiver` and
`drawCustomSurfaceReceiver` callbacks with matching depth/material semantics.
Unknown frontmost geometry is excluded by receiver-depth checks. Transparent
surfaces are not resolved by this opaque screen path.

Legacy dynamic/static point direct light must be inactive; the wrapper refuses
to composite on ownership conflict. Sun, sky and indirect GI remain independent.
The HDR compositor uses premultiplied RGB with zero alpha, preserving destination
alpha. Generic Additive multiplies by source alpha and is not suitable here.

Every automatic scene has a unique material token. Poll GPU completion before
publication; same-command-buffer compute output can be composed immediately.
Confirmed history is reused only for an identical camera/source configuration.
A busy changed view can have no Luma contribution: hosts must account for this
before claiming continuous production lighting. Resources are fenced against
compute AND later graphics readers. On shutdown: finish frames, establish device
idle, poll, drain retired textures, then check `shutdown()` success.

`lighting_luma_gpu` compares actual Metal RGB against analytic Lambert/GGX
values, using 128 co-located sources to force clustered overflow. It covers
on/off, source removal, diffuse-only metallic rejection, legacy ownership,
GGX, BVH blockage and geometry restoration. Build/test artifacts go to KINGSTON.
Native host/editor adoption, mobile/Vulkan implementations and production
performance are separate stages, not implied by this renderer integration.
