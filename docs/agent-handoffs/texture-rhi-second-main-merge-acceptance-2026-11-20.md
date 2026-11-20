# Texture RHI + main — Second merge acceptance

Date: 2026-11-20
Branch: integration/texture-rhi-main-20261120

## Source revisions
Previous integration HEAD: c95f2fdd06571c489d2953791ad5ad0ed4a89ef4
Merged main snapshot: b80601eb3fdcafddedbe9a9387f78a2fea0e499f

## Implementation
Integrated newer main containing SpectraFX with Texture RHI.
Restored Metal writable RGBA32F Texture2D compatibility.
Preserved other writable texture format restrictions.
Added SpectraFX writable texture capability regression assertion.

## Changed compatibility files
- engine/gfx/metal/GfxMetal.mm
- tests/spectrafx_metal_rhi.mm

## Actual Mac Studio tests
- Combined engine build: PASS
- Full CTest: 209/209 PASS, 1514.57 seconds
- SpectraFX focused Metal: 2/2 PASS
- Texture RHI native Metal GPU: 7/7 PASS, 17.38 seconds
- Required GPU execution markers: PASS

## Limitations
- Vulkan/GLES hardware and WebGL2 browser tests are separate.
- Later main changes are not part of this merge.
- The prior standalone SegFault was not deterministically explained.

## Next
Verify merge commit parents and remote branch SHA.
Recheck newer main commits before integration into main.
Keep test evidence tied to the recorded merged snapshot.
