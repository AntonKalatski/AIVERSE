# Local volumetric fog

Opt-in presentation in the common Renderer3D/Prisma pipeline, independent of genre,
TerraWeave and PHYSIS simulation. Includes spheres and quaternion-oriented boxes,
soft inward boundaries and runtime reveal regions. This does not modify gameplay
visibility, fog of war, physics, navigation or deterministic ECS state.

## Native host integration

Call after `Prisma::configure`, on the renderer thread between submissions:

```cpp
#include "engine/render/Prisma.h"
using namespace aiverse::render;

// Host already owns/initializes Prisma and passes it to Renderer3D.
post.configure(aiverse::Json::parse(R"({"hdr":"on"})"), {});
post.setQuality(PrismaQuality::High); // Medium also supported with explicit HDR
LocalFogSettings fog;
fog.enabled = true;
fog.maxDistance = 500;
post.setLocalFog(fog);

FogVolume bank;
bank.id = 1;
bank.region.center = {0, 4, 0};
bank.region.radius = 12;
bank.region.feather = 2;
bank.density = .12f;
bank.color = {.7f, .8f, .9f};
if (!post.localFog().upsert(bank)) { /* invalid input or capacity exhausted */ }

FogVolume corridor;
corridor.id = 2;
corridor.region.shape = FogShape::Obb;
corridor.region.center = {20, 2, 0};
corridor.region.halfExtents = {8, 2, 3}; // half sizes, not full dimensions
corridor.region.orientation = {0, .38268343f, 0, .92387953f}; // x,y,z,w; 45deg Y
corridor.region.feather = .5f;
post.localFog().upsert(corridor);

FogReveal opening;
opening.id = 1; // independent namespace from volume IDs
opening.region.center = {0, 2, 0};
opening.region.radius = 5;
opening.region.feather = 1;
opening.strength = 1;
post.localFog().upsert(opening);

// Each presentation update: move or animate the same ID; no accumulated holes.
opening.region.center = playerPresentationPosition;
opening.strength = animatedRevealStrength; // caller animates 0..1
post.localFog().upsert(opening);
// Restore fog when the reveal ends:
post.localFog().removeReveal(opening.id);
```

A reveal can use `FogShape::Obb` and the same half-extents/orientation fields.
Both types support `upsert`, stable nonzero IDs, and removal. `clear()` removes
all regions. `setLocalFog({})` disables the effect while retaining regions;
`configure` and `shutdown` reset settings AND regions to avoid map leakage.
Check `upsert` return values: at most **8 fog + 8 reveal regions per Prisma**.
Invalid input or overflow leaves existing data intact. Updating an existing ID
works at capacity. Calls are not thread-safe; do not mutate during rendering.

All centers, radii, half-extents and feather widths use **world units** (not the
physical sky's metres conversion); extinction is inverse world units. Radius
and box half-extents must be positive and <=50000, centers within +/-1000000,
feather 0..50000, density 0..100, albedo 0..1 and reveal strength 0..1.
Box quaternions are normalized; nonfinite or zero rotations are rejected.
Sphere rotation is ignored. Feather fades **inward** from the boundary. At a
soft boundary opacity is zero; use a zero-feather reveal or a larger reveal to
fully remove all fog in an identically sized soft-edged volume.

Overlapping fog densities add, with density-weighted albedo. Reveals affect all
local fog, using the maximum weighted reveal strength, so duplicate reveals do
not progressively erase extra density. They do not erase physical sky haze or
clouds. `densityAt(point)` is a CPU query for these cosmetic regions only.
No gameplay sight tests should consume this float data as authority.

## Rendering and limits

Separate full-resolution RGBA16F pass after physical sky, before bloom/tonemap.
Works without physical sky. Uses depth-clipped world-space ray marching,
Beer-Lambert extinction, ambient fill and anisotropic sun scattering with the
existing scene sun shadow map. GLSL/MSL share shader bodies. Geometry shadows
can form local light shafts; no new shadow map or fog self-shadowing is added.
Medium uses 32 samples and High 64 **per merged ray interval**. Disjoint sphere/
box intersections are sorted and merged before marching, avoiding sampling long
empty gaps. Camera-inside, perspective and orthographic rays use the same path.

No Low/LDR fallback, temporal reconstruction, multiple scattering, local-light
scattering, fog shadows on surfaces or reflection/IBL capture. Highly overlapping
large/small volumes and very thin reveal edges can undersample. GPU cost depends
on covered pixels and disjoint intervals; no performance claim is made. As a
post effect, it clips to opaque scene depth; transparent objects without depth
writes are not individually fog-integrated. Sky/cloud/fog overlap is composited,
not a unified participating-media solution.

This stage exposes C++ host authoring/runtime APIs, not map JSON persistence,
Studio authoring controls or automatic RTS/FPS installation. Hosts recreate the
transient regions after map configure/load, from their own authored data.

## Verification

`aiverse_local_fog_tests` / CTest `local_fog` checks region math, rotated OBB,
soft edges, reveal union, runtime movement/removal, bounded transactional updates,
invalid input, quality gating and GPU pipeline lifetime/failure behavior. Passing
one optional filename prefix writes the exact GLSL/MSL used by the mock device.

`AIVERSE_LOCAL_FOG_TEST=off|sphere|reveal|move|obb|obb-reveal|medium|ortho|combined|behind|inside`
in `aiverse_hdr_scene` (Metal) / `aiverse_hdr_gl` selects an opt-in real GPU fixture.
`move` moves the reveal away on frame 3; frame 5 must match `sphere`. Full reveal
must match `off`. `combined` enables sky/clouds and local fog together.
Use `AIVERSE_HDR_CAPTURE=/tmp/fog.ppm` and `AIVERSE_AUDIO=off`.

The legacy `PostFx` alias forwards to `Prisma` and retains these APIs. On main,
atmosphere/fog feed exposure metering, motion/temporal resolve and bloom.
New targets use PRISMA roles 200/201 (DoF retains role 50).
