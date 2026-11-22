# SMAA lookup textures

`AreaTex.h` and `SearchTex.h` are unmodified from https://github.com/iryoku/smaa (MIT, see
LICENSE.txt). The SMAA 1x passes in `engine/render/PrismaShadersExtra.h` (`kSmaaEdge`,
`kSmaaWeight`, `kSmaaBlend`) are a port of the reference `SMAA.hlsl` to the PRISMA GLSL/MSL
common subset (luma edges, orthogonal search, corner rounding; diagonals disabled).
