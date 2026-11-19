// Shaders of the post-processing chain (Prisma.cpp). Each program is written once in a GLSL/MSL
// common subset and wrapped per backend by prisma::buildProgram():
//   * types vec2/vec3/vec4 (MSL: #define to float2..4), no ## pasting (old GLES drivers);
//   * uniforms: one block U0 of vec4 fields, used by name (GLSL block members; MSL locals copied from u);
//   * textures: TEX0(uv) .. TEX4(uv); the output: RETURN(color);
//   * helper functions start with PF_FN (MSL: static);
//   * `s` = screen uv of the pixel (0..1, y up); TUV(p) -> texture uv of a render target
//     (Metal stores targets top-down: px.z = 1 flips v); TDIR(d) flips a direction the same way.
// The vertex stage is a full-screen triangle (one Float2 attribute).
#pragma once

namespace aiverse::render::prisma {

// Bloom bright pass + 4-tap downsample. px.xy = source texel * tap spread; p0 = threshold, knee.
inline const char* kPrefilter = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = (TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
              TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb) * 0.25;
    float br = max(c.r, max(c.g, c.b));
    float k = max(p0.x * p0.y, 0.0001);
    float soft = clamp(br - p0.x + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k + 0.0001);
    float w = max(soft, br - p0.x) / max(br, 0.0001);
    RETURN(vec4(c * w, 1.0));
)";

// Dual-filter downsample (centre x4 + 4 diagonals). px.xy = source texel.
inline const char* kDown = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = TEX0(uv).rgb * 4.0 + TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
             TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb;
    RETURN(vec4(c * 0.125, 1.0));
)";

// Dual-filter upsample of the lower level (tex0) added to this level's downsample (tex1).
// px.xy = texel of tex0; p0.x = weight of the lower level (scatter).
inline const char* kUp = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = TEX0(uv + vec2(-2.0 * o.x, 0.0)).rgb + TEX0(uv + vec2(2.0 * o.x, 0.0)).rgb +
             TEX0(uv + vec2(0.0, -2.0 * o.y)).rgb + TEX0(uv + vec2(0.0, 2.0 * o.y)).rgb +
             (TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb +
              TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb) * 2.0;
    RETURN(vec4(c * (p0.x / 12.0) + TEX1(uv).rgb, 1.0));
)";

// Separable 9-tap gaussian through 5 bilinear taps. px.xy = source texel; p0.xy = direction * spread.
inline const char* kBlur = R"(
    vec2 uv = TUV(s);
    vec2 d = px.xy * p0.xy;
    vec3 c = TEX0(uv).rgb * 0.2270270 +
             (TEX0(uv + d * 1.3846154).rgb + TEX0(uv - d * 1.3846154).rgb) * 0.3162162 +
             (TEX0(uv + d * 3.2307692).rgb + TEX0(uv - d * 3.2307692).rgb) * 0.0702703;
    RETURN(vec4(c, 1.0));
)";

// Sun shafts mask: bright pixels near the sun. px.xy = source texel * spread; p0.x = threshold;
// sun.xy = sun position (texture uv), sun.w = aspect.
inline const char* kShaftsPre = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = (TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
              TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb) * 0.25;
    float l = max(c.r, max(c.g, c.b));
    float m = smoothstep(p0.x, p0.x + 0.15, l);
    vec2 d = uv - sun.xy;
    d.x *= sun.w;
    float fall = clamp(1.0 - length(d) * 0.45, 0.0, 1.0);
    RETURN(vec4(c * (m * fall), 1.0));
)";

// Radial blur towards the sun. p0 = samples, length (0..1), decay, weight; sun.xy = sun (texture uv).
inline const char* kShafts = R"(
    vec2 uv = TUV(s);
    vec2 delta = (uv - sun.xy) * (p0.y / p0.x);
    vec3 acc = vec3(0.0);
    float illum = 1.0;
    vec2 q = uv;
    for (int i = 0; i < 64; i++) {
        if (float(i) >= p0.x) break;
        q -= delta;
        acc += TEX0(q).rgb * illum;
        illum *= p0.z;
    }
    RETURN(vec4(acc * (p0.w / p0.x), 1.0));
)";


// Stage 22 experimental depth-based defocus. TEX0 color, TEX1 scene depth.
// px.xy=1/resolution; px.z=RT UV flip; p0=(focusDepth,range,maxRadius,intensity).
// Hardware depth deliberately remains [0,1] (no unsupported near/far inference).
inline const char* kDepthOfField = R"(
    vec2 uv = TUV(s);
    vec3 original = TEX0(uv).rgb;
    float depth = TEX1(uv).r;
    float coc = clamp((abs(depth - p0.x) - p0.y * 0.5) /
                      max(p0.y, 0.0001), 0.0, 1.0) * p0.w;
    vec2 radius = px.xy * p0.z * coc;
    vec3 sum = original * 2.0;
    sum += TEX0(uv + vec2( radius.x, 0.0)).rgb;
    sum += TEX0(uv + vec2(-radius.x, 0.0)).rgb;
    sum += TEX0(uv + vec2(0.0,  radius.y)).rgb;
    sum += TEX0(uv + vec2(0.0, -radius.y)).rgb;
    sum += TEX0(uv + radius * vec2( 0.7071,  0.7071)).rgb;
    sum += TEX0(uv + radius * vec2(-0.7071,  0.7071)).rgb;
    sum += TEX0(uv + radius * vec2( 0.7071, -0.7071)).rgb;
    sum += TEX0(uv + radius * vec2(-0.7071, -0.7071)).rgb;
    RETURN(vec4(sum * 0.1, 1.0));
)";


// Stage 23: HDR log-luminance pyramid. All pyramid targets use RGBA16F.
// Values are log2(luma) stored in R (signed HDR float, never RGBA8).
// First pass stores weighted log2 luminance in R and total weight in G.
// p0.x = centerWeight (0 uniform, 1 strongly center-biased)
// p0.y = centerRadius (relative normalized image coordinates)
// sun.x/sun.y = min/max metering log2 luminance clamps.
// Later reduction preserves numerator/denominator rather than averaging
// already-normalized values, so center weighting remains correct.
inline const char* kExposureLog = R"(
    vec2 uv = TUV(s);
    vec2 t = px.xy * 0.5;
    vec2 q0 = clamp(uv + vec2(-t.x,-t.y), vec2(0.0), vec2(1.0));
    vec2 q1 = clamp(uv + vec2( t.x,-t.y), vec2(0.0), vec2(1.0));
    vec2 q2 = clamp(uv + vec2(-t.x, t.y), vec2(0.0), vec2(1.0));
    vec2 q3 = clamp(uv + vec2( t.x, t.y), vec2(0.0), vec2(1.0));
    vec3 a = max(TEX0(q0).rgb, vec3(0.0));
    vec3 b = max(TEX0(q1).rgb, vec3(0.0));
    vec3 c = max(TEX0(q2).rgb, vec3(0.0));
    vec3 d = max(TEX0(q3).rgb, vec3(0.0));
    vec4 luminance = vec4(dot(a,vec3(0.2126,0.7152,0.0722)),
                          dot(b,vec3(0.2126,0.7152,0.0722)),
                          dot(c,vec3(0.2126,0.7152,0.0722)),
                          dot(d,vec3(0.2126,0.7152,0.0722)));
    vec4 logL = clamp(log2(max(luminance,vec4(0.0001))),
                      vec4(sun.x),vec4(max(sun.x,sun.y)));
    float radius = max(p0.y,0.05);
    vec2 center = vec2(0.5,0.5);
    vec2 d0 = (q0-center)/radius, d1 = (q1-center)/radius;
    vec2 d2 = (q2-center)/radius, d3 = (q3-center)/radius;
    vec4 weights = mix(vec4(1.0),
                       vec4(exp(-2.0*dot(d0,d0)),exp(-2.0*dot(d1,d1)),
                            exp(-2.0*dot(d2,d2)),exp(-2.0*dot(d3,d3))),
                       clamp(p0.x,0.0,1.0));
    RETURN(vec4(dot(logL,weights)*0.25,
                (weights.x+weights.y+weights.z+weights.w)*0.25,0.0,1.0));
)";

inline const char* kExposureReduce = R"(
    vec2 uv = TUV(s);
    vec2 t = px.xy * 0.5;
    vec2 a = TEX0(clamp(uv + vec2(-t.x,-t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 b = TEX0(clamp(uv + vec2( t.x,-t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 c = TEX0(clamp(uv + vec2(-t.x, t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 d = TEX0(clamp(uv + vec2( t.x, t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 mean = (a+b+c+d)*0.25;
    RETURN(vec4(mean.x,mean.y,0.0,1.0));
)";

// TEX0: 1x1 mean log2(luminance); TEX1: previous 1x1 exposure factor.
// p0=(deltaSeconds, speedUp, speedDown, historyValid).
// sun=(keyValue, minEV, maxEV, compensationEV).
inline const char* kExposureAdapt = R"(
    vec2 weighted = TEX0(vec2(0.5)).rg;
    float logL = weighted.x / max(weighted.y,0.0000001);
    float ev = clamp(log2(max(sun.x,0.0001))-logL+sun.w,sun.y,sun.z);
    float target = exp2(ev);
    float previous = max(TEX1(vec2(0.5)).r,0.0001);
    float rate = target > previous ? p0.y : p0.z;
    float alpha = 1.0 - exp(-max(p0.x,0.0)*max(rate,0.0));
    float result = p0.w > 0.5 ? mix(previous,target,alpha) : target;
    RETURN(vec4(result,0.0,0.0,1.0));
)";


// Camera velocity in normalized screen UV per frame. The inverse current
// viewProjection transforms hardware depth back to world; previous VP then
// projects it to the previous screen. The B channel of the optional TEX2
// object-velocity texture is its presence mask; RG contains current-minus-
// previous screen UV displacement. This lets independent renderers supply
// true object motion without changing deterministic gameplay ECS.
// Matrix uniforms are column-major mat4 columns as 8 vec4 fields.
// px.xy = 1/viewport; px.z = renderTargetFlipY
// p0.x = hardware clipDepthZeroToOne, p0.y = object buffer enabled.
inline const char* kMotionVectors = R"(
    vec2 uv = TUV(s);
    float depth = TEX1(uv).r;
    vec2 velocity = vec2(0.0);
    if (depth < 0.999999) {
        vec4 clip = vec4(s * 2.0 - vec2(1.0), 
                         p0.x > 0.5 ? depth : depth * 2.0 - 1.0, 1.0);
        vec4 world = inv0 * clip.x + inv1 * clip.y +
                     inv2 * clip.z + inv3 * clip.w;
        world /= max(abs(world.w),0.000001) * (world.w < 0.0 ? -1.0 : 1.0);
        vec4 previous = prev0 * world.x + prev1 * world.y +
                        prev2 * world.z + prev3 * world.w;
        if (previous.w > 0.00001) {
            vec2 prevUv = previous.xy / previous.w * 0.5 + 0.5;
            velocity = clamp(s - prevUv,vec2(-1.0),vec2(1.0));
        }
    }
    if (p0.y > 0.5) {
        vec4 objectMotion = TEX2(uv);
        if (objectMotion.b > 0.5) velocity = objectMotion.rg;
    }
    RETURN(vec4(velocity, 0.0, 1.0));
)";

// Full-resolution temporal shutter blur; velocity is from the corresponding
// previous frame and is expressed as current-minus-previous normalized UV.
// Uses current frame samples only (not temporal color accumulation), so no
// ghost-producing history reads and no persistent color state.
inline const char* kMotionBlur = R"(
    vec2 uv = TUV(s);
    vec2 velocity = TEX1(uv).rg * p0.x;
    vec2 pixels = velocity / max(px.xy,vec2(0.000001));
    float distancePx = length(pixels);
    float scale = min(1.0, p0.y / max(distancePx,0.0001));
    velocity *= scale;
    int n = int(clamp(floor(p0.z + 0.5),2.0,16.0));
    // Scene depth is supplied as TEX2. Reject far-away samples where a
    // near silhouette would otherwise smear their color over foreground.
    // Device-depth threshold is intentionally conservative, independent of
    // the camera near/far planes; exact linear-depth filtering comes later.
    float centerDepth = TEX2(uv).r;
    vec3 accum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (i >= n) break;
        float t = float(i) / float(n-1) - 0.5;
        vec2 q = clamp(s + velocity * t,vec2(0.0),vec2(1.0));
        vec2 sampleUv = TUV(q);
        float sampleDepth = TEX2(sampleUv).r;
        float depthDelta = abs(sampleDepth - centerDepth);
        float keep = 1.0 - smoothstep(p0.y > 0.0 ? 0.0015 : 0.0,
                                      0.012, depthDelta);
        accum += TEX0(sampleUv).rgb * keep;
        weight += keep;
    }
    vec3 sharp = TEX0(uv).rgb;
    RETURN(vec4(mix(sharp,accum/max(weight,0.0001),p0.w),1.0));
)";

// FXAA (console-style, 5 taps + early out, 9 at most). px.xy = texel; p0.x = subpixel softening.
inline const char* kFxaaHelpers = R"(
PF_FN float pfLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
)";
inline const char* kFxaa = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 cM = TEX0(uv).rgb;
    float lNW = pfLuma(TEX0(uv + vec2(-o.x, -o.y)).rgb), lNE = pfLuma(TEX0(uv + vec2(o.x, -o.y)).rgb);
    float lSW = pfLuma(TEX0(uv + vec2(-o.x, o.y)).rgb), lSE = pfLuma(TEX0(uv + vec2(o.x, o.y)).rgb);
    float lM = pfLuma(cM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) RETURN(vec4(cM, 1.0));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * o;
    vec3 a = 0.5 * (TEX0(uv + dir * (1.0 / 3.0 - 0.5)).rgb + TEX0(uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (TEX0(uv - dir * 0.5).rgb + TEX0(uv + dir * 0.5).rgb);
    float lB = pfLuma(b);
    vec3 r = (lB < lMin || lB > lMax) ? a : b;
    RETURN(vec4(mix(r, cM, (1.0 - p0.x) * 0.5), 1.0));
)";

// Final composite. PF_* defines pick the effects (one program per combination, built on demand).
//   px      texel x, y, flip, grain seed     grade0  -, contrast, saturation, posterize levels
//   grade1  colour multiplier rgb, pixel size lift    lift rgb, vignette intensity
//   gamma   1/gamma rgb, vignette softness   gain    gain rgb, grain intensity
//   filt    -, -, -, grain size              vig     vignette colour rgb, aberration
//   bloom   bloom tint rgb, sharpen amount   shafts  shafts colour rgb, LUT intensity
//   tilt    focus, width, falloff, -          fade    colour rgb, amount
//   lut     size N, flipY, -, -               lbox    colour rgb, bars height
// Textures: 0 scene, 1 bloom, 2 tilt-shift blur, 3 sun shafts, 4 LUT.
inline const char* kCompositeHelpers = R"(
PF_FN float pfHash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
)";
inline const char* kComposite = R"(
    vec2 sp = s;
#ifdef PF_PIXELATE
    {
        vec2 cell = px.xy * grade1.w;
        sp = (floor(sp / cell) + 0.5) * cell;
    }
#endif
    vec2 uv = TUV(sp);
#ifdef PF_CA
    vec2 cd = TDIR((sp - 0.5) * (vig.w * 0.012));
    vec3 col = vec3(TEX0(uv + cd).r, TEX0(uv).g, TEX0(uv - cd).b);
#else
    vec3 col = TEX0(uv).rgb;
#endif
#ifdef PF_SHARPEN
    {
        vec2 o = px.xy * 0.66;
        vec3 n = (TEX0(uv + o).rgb + TEX0(uv - o).rgb) * 0.5;
        col = max(col + (col - n) * (bloom.w * 1.5), vec3(0.0));
    }
#endif
#ifdef PF_TILT
    {
        float dy = abs(sp.y - tilt.x);
        float m = smoothstep(tilt.y * 0.5, tilt.y * 0.5 + tilt.z + 0.0001, dy);
        col = mix(col, TEX2(uv).rgb, m);
    }
#endif
#ifdef PF_BLOOM
    col += TEX1(uv).rgb * bloom.rgb;
#endif
#ifdef PF_SHAFTS
    col += TEX3(uv).rgb * shafts.rgb;
#endif
#ifdef PF_AUTO_EXPOSURE
    // HDR-only 1x1 GPU-adapted exposure: sample before ACES/sRGB conversion.
    col *= clamp(TEX8(vec2(0.5)).r, 0.00024414, 4096.0);
#endif
    if (filt.x > 0.5) {
        col = max(col * filt.z, vec3(0.0));
        if (filt.y > 0.5) col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
        col = rtsSrgbEncode(clamp(col, vec3(0.0), vec3(1.0)));
    }
#ifdef PF_GRADE
    col *= grade1.rgb;
    col = gain.rgb * (col + lift.rgb * (vec3(1.0) - col));
    col = pow(max(col, vec3(0.0)), gamma.rgb);
    col = (col - vec3(0.5)) * grade0.y + vec3(0.5);
    col = mix(vec3(dot(col, vec3(0.2126, 0.7152, 0.0722))), col, grade0.z);
    col = clamp(col, vec3(0.0), vec3(1.0));
#endif
#ifdef PF_LUT
    {
        vec3 c = clamp(col, vec3(0.0), vec3(1.0));
        float n = lut.x;
        float bz = c.b * (n - 1.0);
        float s0 = floor(bz);
        float s1 = min(s0 + 1.0, n - 1.0);
        float x = (c.r * (n - 1.0) + 0.5) / (n * n);
        float y = (c.g * (n - 1.0) + 0.5) / n;
        if (lut.y > 0.5) y = 1.0 - y;
        vec3 la = TEX4(vec2(x + s0 / n, y)).rgb;
        vec3 lb = TEX4(vec2(x + s1 / n, y)).rgb;
        col = mix(col, mix(la, lb, bz - s0), shafts.w);
    }
#endif
#ifdef PF_POSTERIZE
    col = floor(col * grade0.w + vec3(0.5)) / grade0.w;
#endif
#ifdef PF_VIGNETTE
    {
        float aspect = px.y / px.x;
        vec2 d = (s - vec2(0.5)) * vec2(aspect, 1.0);
        float r = length(d) / length(vec2(aspect, 1.0) * 0.5);
        float m = smoothstep(mix(0.85, 0.0, gamma.w), 1.0, r);
        col = mix(col, vig.rgb, m * lift.w);
    }
#endif
#ifdef PF_GRAIN
    {
        vec2 g = floor(s / (px.xy * filt.w));
        col += vec3((pfHash(g + px.w * vec2(17.0, 31.0)) - 0.5) * gain.w);
    }
#endif
#ifdef PF_FADE
    col = mix(col, fade.rgb, fade.w);
#endif
#ifdef PF_LETTERBOX
    if (s.y < lbox.w * 0.5 || s.y > 1.0 - lbox.w * 0.5) col = lbox.rgb;
#endif
    col += vec3((pfHash(s * 1931.0 + px.w) - 0.5) / 255.0);   // dither: no banding in gradients / vignette
    RETURN(vec4(clamp(col, vec3(0.0), vec3(1.0)), 1.0));
)";

} // namespace aiverse::render::postfx
