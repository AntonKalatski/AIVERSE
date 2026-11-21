# PRISMA: water on the camera lens

The optional `lensWater` effect is independent of NEREID underwater absorption.
It is absent from old profiles and therefore OFF by default. Add it through the
existing PRISMA effect catalog/editor/MCP or the project/map `postfx` profile:

```json
{"type":"lensWater","enabled":true,"amount":0.75,"dropSize":16,
 "density":0.55,"distortion":12,"flowSpeed":24,"dryTime":4,"film":0.2}
```

Set `enabled:false`, remove the effect, or set `amount:0` to disable it. Disabling
clears wetness on the next camera update and leaves underwater optics unchanged.
`dropSize` is maximum radius in output pixels; individual droplets vary in size.
`distortion` is refraction offset in output pixels; `flowSpeed` is pixels/second
downward (0 makes beads stationary); `dryTime` is seconds after leaving water.
`density`, `amount`, and `film` range0..1. Generic catalog parameter bounds and
camera-volume overrides apply; default minimum quality is Medium.

Native hosts call the existing `Prisma::setUnderwater` after `setViewId` and camera
profile resolution on every frame, including dry frames. NEREID provides the
read-only immersion snapshot through `NereidManifestHost::updateUnderwater` or
`updateNereidPrismaUnderwater`. PRISMA alone owns lens history and image distortion.
A submerged camera wets its own lens; exiting begins drainage/drying. A camera
that has never entered water does not acquire droplets simply by enabling the
profile. Call `retireView` on camera destruction. At most64 wet camera histories
are retained; a further camera gets no new wet-lens history until one retires or
dries. This never changes simulation, water flow, terrain, ECS saves or physics.

The existing composite owns the shared GLSL/MSL effect: bounded9-cell procedural
droplet search, variable radius/jitter, two scene-color samples, draining film,
no new GPU targets, images or extra full-screen pass. Composite U0 adds two vec4s;
field and feature define counts derive from the arrays. The work estimate is
heuristic, not a measured performance claim. It is a first screen-space lens
approximation, not physical wetting/contact-angle simulation or full reference
video parity. Surface waves/meniscus still belong to NEREID geometry plus PRISMA
optical transition, and need separate visual acceptance.
