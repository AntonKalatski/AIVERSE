# Physical sky, volumetric clouds and god rays

The optional `PostFx` atmosphere pass renders a spherical, exponential Earth
atmosphere with Rayleigh/Mie single scattering, a sun disk, ray-marched 3D
procedural clouds, Beer–Lambert extinction, cloud self-shadowing and
Henyey–Greenstein directional scattering. God rays march the view segment and
sample both cloud transmittance and the renderer's sun shadow map. This is
volumetric in-scattering, independent of the legacy screen-space `sunShafts` blur.

## Enable

Merge [the example](../examples/render/physical-sky.json) into the project's
`game.json` or map. `postfx.physicalSky` in the map overrides individual game
fields; `enabled:false` disables it. Old files remain unchanged. Use HDR (`hdr:on`)
and Medium or High post quality. Off/Low, unsupported RGBA16F, HDR explicitly off,
and target/shader failure retain the ordinary scene. The existing editor
post-processing preview must be enabled for Scene; Game uses its post chain.
There is no dedicated sky authoring panel in this stage.

The main sun direction, color and intensity come from `Renderer3D::lighting`.
Use a low sun angle and broken coverage for visible shafts; turn off the old
`sunShafts` effect and `ScreenLighting::VolumetricLight` when using this atmospheric
haze to avoid additive double scattering. Scene shadows are optional: cloud
shafts still work without them. Outside the sun shadow frustum, geometry
occlusion is unavailable and only cloud occlusion applies.

Native hosts link the existing runtime, include `engine/render/PostFx.h`, call
`PostFx::setPhysicalSky(settings)`, and pass that post processor to
`Renderer3D::render`; call `resolvePostFx` after scene rendering as usual. Any
host can use this API; clients without a PostProcess hook must explicitly wire
one. No FPS/RTS gameplay or ECS save format is changed.

## Parameters

| Field | Meaning / default |
|---|---|
| enabled / clouds / godRays | Overall opt-in (false), clouds (true), shafts/haze (true) |
| metersPerUnit / seaLevel | Metres per world unit (1); sea level in metres (0) |
| rayleigh / mie | Scattering coefficient multipliers (1 / 1) |
| sunRadiance | Solar radiance scale (20), multiplied by the main sun |
| cloudBase / cloudThickness | Layer base and thickness in metres (1500 / 1500) |
| cloudScale | Noise scale in metres (2400) |
| coverage | Procedural density threshold control, 0..1 (0.45); zero is cloud-free |
| extinction | Cloud extinction per metre (0.003) |
| anisotropy | Cloud phase asymmetry, -0.9..0.9 (0.65) |
| windX / windZ | Visual advection in metres per second (12 / 4) |
| haze / rayDistance | Air extinction per metre (0.00001); shaft reach cap (8000 m) |

Nonfinite settings are replaced with defaults; finite out-of-range values are
clamped. Wrong JSON types and unknown fields produce configuration issues.
`configure` resets settings and the animation epoch between maps/projects.
`setPhysicalSkyTime(seconds)` freezes animation for reproducible screenshots;
a negative value resumes the presentation clock. Sampling never advances PHYSIS.
A native PHYSIS consumer may map its committed read-only weather snapshot into
coverage/wind settings; no automatic weather adapter is installed.

## Rendering and cost

One full-resolution HDR pass runs before bloom, grading, underwater composition
and tone mapping, reading a separate scene color/depth target. Foreground depth
clips clouds and air. Near/far unprojection supports perspective and orthographic
views, with backend-specific texture orientation and clip depth. Cameras may be
below, inside or above the flat cloud layer. Intermediate targets share PostFx's
bounded size-cache lifecycle; sampled depth is never a writable attachment.

Medium uses 12 atmosphere / 48 cloud / 4 cloud-light / 16 air steps; High uses
20 / 96 / 6 / 32. Cloud integration exits early when opaque. The pass is expensive
at large resolutions; these are work bounds, not measured frame-time guarantees.
No temporal history is retained, avoiding ghosting and cross-view weather history.

This first implementation uses single scattering, a flat cloud slab and a
bounded 50 km visible cloud range. It has no multiple-scattering LUT, temporal
upsampling, planetary cloud shell, cloud shadows on surface materials, sky IBL
rebake or reflection-probe integration. Air/cloud compositing is an approximation,
not a fully coupled participating-media solver. Alpha-blended geometry without
usable scene depth cannot provide exact volume ordering. Existing fog/ambient
surface lighting remains host-owned. No weather simulation is implemented here.

## Verification

`./run-tests.sh --filter '^physical_sky$'` reaches the CPU configuration, quality,
resource lifetime, framebuffer-feedback and shader-failure regression test.
The ordinary HDR GPU fixture accepts `AIVERSE_PHYSICAL_SKY_TEST` values `clouds`,
`clear`, `no-rays`, `sunset`, `inside`, `above`, `medium`, `ortho`, `night`, with fixed animation time and
`AIVERSE_HDR_CAPTURE` as the output path. Build `aiverse_hdr_scene` with
`AIVERSE_BUILD_HDR_TESTS=ON`; run the same fixture on Metal and `aiverse_hdr_gl`
for GL. Exact executed results and remaining platforms are in
[the handoff](agent-handoffs/physical-sky-clouds-2026-11-20.md).
