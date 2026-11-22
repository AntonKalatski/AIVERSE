// PRISMA effects added 2026-11-22: screen-space AO and reflections, lens flare ghosts,
// SMAA 1x, FSR 1 (EASU + RCAS), histogram exposure metering. Same GLSL/MSL common
// subset as PrismaShaders.h (see its header): vec types, TEX<n>(uv), RETURN(c), PF_FN
// helpers, `s` = screen uv (y up), TUV(p) = texture uv of a render target.
//
// All passes of this file share one uniform block (Prisma.cpp ExtraU):
//   px   = 1/output width, 1/output height, flip, frame seed
//   p0..p3 = per-pass parameters (documented per program)
//   vp0..vp3  = columns of the current view-projection
//   ivp0..ivp3 = columns of its inverse
//   cam  = camera world position xyz, w = 1 when clip depth is [0,1] (Metal/Vulkan), 0 for GL [-1,1]
#pragma once

namespace aiverse::render::prisma {

// World position of a screen point at device depth d, and back to screen + device depth.
inline const char* kExtraHelpers = R"(
PF_FN vec3 pxWorld(vec2 sp, float d, vec4 i0, vec4 i1, vec4 i2, vec4 i3, float zeroToOne) {
    float z = zeroToOne > 0.5 ? d : d * 2.0 - 1.0;
    vec4 h = i0 * (sp.x * 2.0 - 1.0) + i1 * (sp.y * 2.0 - 1.0) + i2 * z + i3;
    return h.xyz / (abs(h.w) > 0.0000001 ? h.w : 0.0000001);
}
PF_FN vec3 pxProject(vec3 p, vec4 v0, vec4 v1, vec4 v2, vec4 v3, float zeroToOne) {
    vec4 c = v0 * p.x + v1 * p.y + v2 * p.z + v3;
    float w = c.w > 0.0000001 ? c.w : 0.0000001;
    vec3 n = c.xyz / w;
    return vec3(n.xy * 0.5 + 0.5, zeroToOne > 0.5 ? n.z : n.z * 0.5 + 0.5);
}
PF_FN float pxHash(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
PF_FN float pxLuma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
)";

// ---- Ambient occlusion buffer (size / div). TEX0 = scene device depth (full size).
// p0 = (radius world, intensity, bias, power)
// p1 = (directions, steps, maxDistance, maxPixels)
// p2.xy = 1/depth width, 1/depth height
// Output: R = visibility (1 = open), G = 1 when the pixel is sky.
inline const char* kAo = R"(
    vec2 uv = TUV(s);
    float d = TEX0(uv).r;
    if (d >= 0.99999) RETURN(vec4(1.0, 1.0, 0.0, 1.0));
    vec3 P = pxWorld(s, d, ivp0, ivp1, ivp2, ivp3, cam.w);
    // Normals over a 3-texel baseline: one-texel differences of a grazing
    // floor are dominated by depth quantisation (banding).
    vec2 e = p2.xy * 3.0;
    // Normal from the smaller of the two one-sided depth differences on each
    // axis: no halo of wrong normals along silhouettes.
    float dR = TEX0(TUV(s + vec2(e.x, 0.0))).r, dL = TEX0(TUV(s - vec2(e.x, 0.0))).r;
    float dU = TEX0(TUV(s + vec2(0.0, e.y))).r, dD = TEX0(TUV(s - vec2(0.0, e.y))).r;
    vec3 pR = pxWorld(s + vec2(e.x, 0.0), dR, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pL = pxWorld(s - vec2(e.x, 0.0), dL, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pU = pxWorld(s + vec2(0.0, e.y), dU, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pD = pxWorld(s - vec2(0.0, e.y), dD, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 ddx = abs(dR - d) < abs(d - dL) ? pR - P : P - pL;
    vec3 ddy = abs(dU - d) < abs(d - dD) ? pU - P : P - pD;
    vec3 N = normalize(cross(ddx, ddy));
    vec3 toCam = cam.xyz - P;
    float viewDist = length(toCam);
    vec3 V = toCam / max(viewDist, 0.0001);
    if (dot(N, V) < 0.0) N = -N;
    // Screen radius of the world radius at this depth.
    vec3 T = normalize(cross(V, abs(V.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec2 rs = pxProject(P + T * p0.x, vp0, vp1, vp2, vp3, cam.w).xy - s;
    float radiusPx = min(length(rs / p2.xy), p1.w);
    if (radiusPx < 1.0) RETURN(vec4(1.0, 0.0, 0.0, 1.0));
    // Lift the origin off the surface by the depth-precision error at this
    // distance: coplanar neighbours then read as open, not as banded acne on
    // grazing floors.
    P += N * (viewDist * 0.01 + p0.x * 0.02);
    float noise = pxHash(floor(s / p2.xy) + vec2(px.w * 7.0, px.w * 3.0));
    float noise2 = pxHash(floor(s / p2.xy) * 1.7 + vec2(19.0, 47.0));
    float occlusion = 0.0, samples = 0.0;
    float r2 = p0.x * p0.x;
    for (int i = 0; i < 8; i++) {
        if (float(i) >= p1.x) break;
        float angle = (float(i) + noise) * 6.2831853 / p1.x;
        vec2 dir = vec2(cos(angle), sin(angle));
        for (int j = 0; j < 8; j++) {
            if (float(j) >= p1.y) break;
            float t = (float(j) + 0.5 + noise2 * 0.5) / p1.y;
            vec2 q = s + dir * p2.xy * (radiusPx * t);
            if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) continue;
            float ds = TEX0(TUV(q)).r;
            if (ds >= 0.99999) { samples += 1.0; continue; }
            vec3 v = pxWorld(q, ds, ivp0, ivp1, ivp2, ivp3, cam.w) - P;
            float dist2 = dot(v, v);
            float cosine = dot(N, v) / sqrt(dist2 + 0.000001);
            float falloff = clamp(1.0 - dist2 / r2, 0.0, 1.0);
            occlusion += max(cosine - p0.z, 0.0) * falloff;
            samples += 1.0;
        }
    }
    float ao = 1.0 - occlusion / max(samples, 1.0) * 2.0 * p0.y;
    ao = pow(clamp(ao, 0.0, 1.0), p0.w);
    // Fade out in the distance (depth precision, fog).
    ao = mix(ao, 1.0, smoothstep(p1.z * 0.7, p1.z, viewDist));
    RETURN(vec4(ao, 0.0, 0.0, 1.0));
)";

// ---- Depth-aware AO upsample + apply. TEX0 = colour, TEX1 = AO buffer, TEX2 = depth.
// p0 = (1/ao width, 1/ao height, near, far)  p1 = (orthographic, -, -, -)
// p2 = occlusion colour rgb, -
inline const char* kAoApplyHelpers = R"(
PF_FN float pxLinearDepth(float d, float n, float f, float ortho) {
    if (ortho > 0.5) return mix(n, f, d);
    return n * f / max(f - d * (f - n), 0.0001);
}
)";
inline const char* kAoApply = R"(
    vec2 uv = TUV(s);
    vec4 color = TEX0(uv);
    float d = TEX2(uv).r;
    if (d >= 0.99999) RETURN(color);
    float z = pxLinearDepth(d, p0.z, p0.w, p1.x);
    float sum = 0.0, weight = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 q = s + vec2(float(x), float(y)) * p0.xy;
            float a = TEX1(TUV(q)).r;
            float zq = pxLinearDepth(TEX2(TUV(q)).r, p0.z, p0.w, p1.x);
            float w = exp(-abs(zq - z) / max(z * 0.03, 0.001)) * (x == 0 && y == 0 ? 2.0 : 1.0);
            sum += a * w;
            weight += w;
        }
    }
    float ao = sum / max(weight, 0.0001);
    RETURN(vec4(mix(p2.rgb * color.rgb, color.rgb, ao), color.a));
)";

// ---- Screen-space reflections. TEX0 = colour, TEX1 = device depth.
// p0 = (intensity, maxDistance, thickness, steps)
// p1 = (minNormalY, fresnel, edgeFade, refine steps)
// p2 = (1/depth width, 1/depth height, roughness, -)
inline const char* kSsr = R"(
    vec2 uv = TUV(s);
    vec4 color = TEX0(uv);
    float d = TEX1(uv).r;
    if (d >= 0.99999 || p0.x <= 0.0) RETURN(color);
    vec3 P = pxWorld(s, d, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec2 e = p2.xy;
    float dR = TEX1(TUV(s + vec2(e.x, 0.0))).r, dL = TEX1(TUV(s - vec2(e.x, 0.0))).r;
    float dU = TEX1(TUV(s + vec2(0.0, e.y))).r, dD = TEX1(TUV(s - vec2(0.0, e.y))).r;
    vec3 pR = pxWorld(s + vec2(e.x, 0.0), dR, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pL = pxWorld(s - vec2(e.x, 0.0), dL, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pU = pxWorld(s + vec2(0.0, e.y), dU, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 pD = pxWorld(s - vec2(0.0, e.y), dD, ivp0, ivp1, ivp2, ivp3, cam.w);
    vec3 ddx = abs(dR - d) < abs(d - dL) ? pR - P : P - pL;
    vec3 ddy = abs(dU - d) < abs(d - dD) ? pU - P : P - pD;
    vec3 N = normalize(cross(ddx, ddy));
    vec3 toCam = cam.xyz - P;
    vec3 V = toCam / max(length(toCam), 0.0001);
    if (dot(N, V) < 0.0) N = -N;
    if (N.y < p1.x) RETURN(color);
    vec3 R = reflect(-V, N);
    float cosTheta = clamp(dot(N, V), 0.0, 1.0);
    float schlick = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);
    float reflectance = p0.x * mix(1.0, schlick, p1.y);
    float jitter = pxHash(floor(s / e) + vec2(px.w * 5.0, px.w * 11.0));
    P += N * (length(toCam) * 0.002);
    vec3 prev = P;
    float prevT = 0.0;
    vec3 hitUv = vec3(-1.0);
    float hitT = 0.0;
    for (int i = 0; i < 96; i++) {
        if (float(i) >= p0.w) break;
        // Quadratic spacing: fine steps near the surface (contact reflections),
        // coarse ones far away.
        float f = (float(i) + 0.5 + jitter * 0.5) / max(p0.w, 1.0);
        float t = f * f * p0.y;
        vec3 Q = P + R * t;
        vec3 q = pxProject(Q, vp0, vp1, vp2, vp3, cam.w);
        if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0 || q.z < 0.0 || q.z > 1.0) break;
        float sd = TEX1(TUV(q.xy)).r;
        if (sd >= 0.99999) { prev = Q; prevT = t; continue; }
        vec3 S = pxWorld(q.xy, sd, ivp0, ivp1, ivp2, ivp3, cam.w);
        float delta = length(Q - cam.xyz) - length(S - cam.xyz);
        if (delta > 0.0 && delta < max(p0.z, (t - prevT) * 1.5)) {
            // Binary refinement between the last free point and the hit.
            vec3 a = prev, b = Q;
            for (int k = 0; k < 6; k++) {
                if (float(k) >= p1.w) break;
                vec3 m = (a + b) * 0.5;
                vec3 mq = pxProject(m, vp0, vp1, vp2, vp3, cam.w);
                float md = TEX1(TUV(mq.xy)).r;
                vec3 ms = pxWorld(mq.xy, md, ivp0, ivp1, ivp2, ivp3, cam.w);
                if (length(m - cam.xyz) - length(ms - cam.xyz) > 0.0) b = m; else a = m;
            }
            hitUv = pxProject(b, vp0, vp1, vp2, vp3, cam.w);
            hitT = t / p0.y;
            break;
        }
        prev = Q;
        prevT = t;
    }
    if (hitUv.x < 0.0) RETURN(color);
    vec2 edge = min(hitUv.xy, vec2(1.0) - hitUv.xy);
    float fade = smoothstep(0.0, p1.z, min(edge.x, edge.y)) * (1.0 - hitT * hitT);
    vec3 reflected = TEX0(TUV(hitUv.xy)).rgb;
    if (p2.z > 0.001) {
        // Cheap roughness: four taps spread with the travelled distance.
        vec2 spread = e * (1.0 + 24.0 * p2.z * hitT * 4.0);
        reflected = (reflected * 2.0 +
                     TEX0(TUV(hitUv.xy + vec2(spread.x, 0.0))).rgb + TEX0(TUV(hitUv.xy - vec2(spread.x, 0.0))).rgb +
                     TEX0(TUV(hitUv.xy + vec2(0.0, spread.y))).rgb + TEX0(TUV(hitUv.xy - vec2(0.0, spread.y))).rgb) / 6.0;
        fade *= 1.0 - 0.5 * p2.z;
    }
    RETURN(vec4(mix(color.rgb, reflected, clamp(reflectance * fade, 0.0, 1.0)), color.a));
)";

// ---- Lens flare ghosts + halo from the bright-pass image (TEX0, small size).
// p0 = (ghosts, ghostSpacing, halo, haloWidth)
// p1 = (chromatic, aspect, -, -)
inline const char* kFlareHelpers = R"(
// Helpers cannot sample (MSL textures are fragment arguments): they return uvs only.
PF_FN vec2 pxFlareUv(vec2 q, float flip) {
    vec2 c = clamp(q, vec2(0.0), vec2(1.0));
    return flip > 0.5 ? vec2(c.x, 1.0 - c.y) : c;
}
#define PX_FLARE(q, o) vec3(TEX0(pxFlareUv((q) + (o), px.z)).r, TEX0(pxFlareUv((q), px.z)).g, TEX0(pxFlareUv((q) - (o), px.z)).b)
)";
inline const char* kFlareGhost = R"(
    vec2 st = vec2(1.0) - s;                // flip through the lens centre
    vec2 ghostVec = (vec2(0.5) - st) * p0.y;
    vec2 dir = normalize(ghostVec + vec2(0.000001));
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        if (float(i) >= p0.x) break;
        vec2 q = fract(st + ghostVec * float(i));
        float r = length(vec2(0.5) - q) / 0.70710678;
        float w = pow(max(1.0 - r, 0.0), 8.0);
        // Ghost colour ring: warm near the centre, blue-violet at the rim.
        vec3 tint = mix(vec3(1.0, 0.85, 0.6), vec3(0.55, 0.7, 1.0), clamp(r * 1.6, 0.0, 1.0));
        acc += PX_FLARE(q, dir * px.xy * p1.x * 6.0) * w * tint;
    }
    vec2 hv = dir * p0.w;
    hv.x /= max(p1.y, 0.0001);
    vec2 hq = st + hv;
    float hr = length(vec2(0.5) - fract(hq)) / 0.70710678;
    float hw = pow(max(1.0 - hr, 0.0), 5.0) * p0.z;
    acc += PX_FLARE(hq, dir * px.xy * p1.x * 12.0) * hw;
    RETURN(vec4(acc, 1.0));
)";

// ---- SMAA 1x (Jimenez et al., MIT; third_party/smaa). Texture-space version of the
// reference passes: all coordinates below are texture uv, so the image orientation of
// the backend does not matter. Diagonal detection is disabled (SMAA "high" preset
// without diagonals); corner detection is kept.
// Edges: TEX0 = colour (display-referred). p0.x = threshold.
inline const char* kSmaaEdge = R"(
    vec2 t = TUV(s);
    vec2 o = px.xy;
    vec3 W = vec3(0.2126, 0.7152, 0.0722);
    float L = dot(TEX0(t).rgb, W);
    float Lleft = dot(TEX0(t + vec2(-o.x, 0.0)).rgb, W);
    float Ltop = dot(TEX0(t + vec2(0.0, -o.y)).rgb, W);
    vec2 delta = abs(vec2(L - Lleft, L - Ltop));
    vec2 edges = step(vec2(p0.x), delta);
    if (edges.x + edges.y == 0.0) RETURN(vec4(0.0, 0.0, 0.0, 1.0));
    float Lright = dot(TEX0(t + vec2(o.x, 0.0)).rgb, W);
    float Lbottom = dot(TEX0(t + vec2(0.0, o.y)).rgb, W);
    vec2 maxDelta = max(delta, abs(vec2(L - Lright, L - Lbottom)));
    float Lleftleft = dot(TEX0(t + vec2(-2.0 * o.x, 0.0)).rgb, W);
    float Ltoptop = dot(TEX0(t + vec2(0.0, -2.0 * o.y)).rgb, W);
    maxDelta = max(maxDelta, abs(vec2(Lleft - Lleftleft, Ltop - Ltoptop)));
    float finalDelta = max(maxDelta.x, maxDelta.y);
    // Local contrast adaptation (factor 2.0 of the reference).
    edges *= step(vec2(finalDelta), 2.0 * delta);
    RETURN(vec4(edges, 0.0, 1.0));
)";

// Blend weights: TEX0 = edges, TEX1 = AreaTex (160x560 RG), TEX2 = SearchTex (64x16 R).
// p0 = (maxSearchSteps, cornerRounding 0..1, -, -); px.xy = texel of the edges image.
inline const char* kSmaaWeightHelpers = R"(
PF_FN vec2 pxSmaaSearchUv(vec2 e, float offset) {
    vec2 scale = vec2(66.0, 33.0) * vec2(0.5, -1.0);
    vec2 bias = vec2(66.0, 33.0) * vec2(offset, 1.0);
    scale += vec2(-1.0, 1.0);
    bias += vec2(0.5, -0.5);
    scale *= 1.0 / vec2(64.0, 16.0);
    bias *= 1.0 / vec2(64.0, 16.0);
    return scale * e + bias;
}
PF_FN vec2 pxSmaaAreaUv(vec2 dist, float e1, float e2) {
    vec2 tc = vec2(16.0) * floor(4.0 * vec2(e1, e2) + vec2(0.5)) + dist;
    return tc * (1.0 / vec2(160.0, 560.0)) + 0.5 * (1.0 / vec2(160.0, 560.0));
}
)";
inline const char* kSmaaWeight = R"(
    vec2 t = TUV(s);
    vec2 o = px.xy;
    vec2 size = vec2(1.0) / o;
    vec2 pix = t * size;
    float steps = p0.x;
    vec4 off0 = vec4(t.x - 0.25 * o.x, t.y - 0.125 * o.y, t.x + 1.25 * o.x, t.y - 0.125 * o.y);
    vec4 off1 = vec4(t.x - 0.125 * o.x, t.y - 0.25 * o.y, t.x - 0.125 * o.x, t.y + 1.25 * o.y);
    vec4 off2 = vec4(off0.x - 2.0 * o.x * steps, off0.z + 2.0 * o.x * steps,
                     off1.y - 2.0 * o.y * steps, off1.w + 2.0 * o.y * steps);
    vec4 weights = vec4(0.0);
    vec2 e = TEX0(t).rg;
    float rounding = 1.0 - p0.y;
    if (e.g > 0.0) {
        // Left.
        vec2 q = off0.xy;
        vec2 ee = vec2(0.0, 1.0);
        for (int i = 0; i < 64; i++) {
            if (!(q.x > off2.x && ee.g > 0.8281 && ee.r == 0.0)) break;
            ee = TEX0(q).rg;
            q.x -= 2.0 * o.x;
        }
        float left = q.x + o.x * (3.25 - (255.0 / 127.0) * TEX2(pxSmaaSearchUv(ee, 0.0)).r);
        // Right.
        q = off0.zw;
        ee = vec2(0.0, 1.0);
        for (int i = 0; i < 64; i++) {
            if (!(q.x < off2.y && ee.g > 0.8281 && ee.r == 0.0)) break;
            ee = TEX0(q).rg;
            q.x += 2.0 * o.x;
        }
        float right = q.x - o.x * (3.25 - (255.0 / 127.0) * TEX2(pxSmaaSearchUv(ee, 0.5)).r);
        vec2 d = abs(floor(size.x * vec2(left, right) - vec2(pix.x) + vec2(0.5)));
        float e1 = TEX0(vec2(left, off1.y)).r;
        float e2 = TEX0(vec2(right + o.x, off1.y)).r;
        weights.rg = TEX1(pxSmaaAreaUv(sqrt(d), e1, e2)).rg;
        // Corner pattern.
        vec2 lr = step(d.xy, d.yx);
        vec2 r = rounding * lr / max(lr.x + lr.y, 1.0);
        vec2 factor = vec2(1.0);
        factor.x -= r.x * TEX0(vec2(left, t.y + o.y)).r;
        factor.x -= r.y * TEX0(vec2(right + o.x, t.y + o.y)).r;
        factor.y -= r.x * TEX0(vec2(left, t.y - 2.0 * o.y)).r;
        factor.y -= r.y * TEX0(vec2(right + o.x, t.y - 2.0 * o.y)).r;
        weights.rg *= clamp(factor, vec2(0.0), vec2(1.0));
    }
    if (e.r > 0.0) {
        // Up.
        vec2 q = off1.xy;
        vec2 ee = vec2(1.0, 0.0);
        for (int i = 0; i < 64; i++) {
            if (!(q.y > off2.z && ee.r > 0.8281 && ee.g == 0.0)) break;
            ee = TEX0(q).rg;
            q.y -= 2.0 * o.y;
        }
        float top = q.y + o.y * (3.25 - (255.0 / 127.0) * TEX2(pxSmaaSearchUv(ee.gr, 0.0)).r);
        // Down.
        q = off1.zw;
        ee = vec2(1.0, 0.0);
        for (int i = 0; i < 64; i++) {
            if (!(q.y < off2.w && ee.r > 0.8281 && ee.g == 0.0)) break;
            ee = TEX0(q).rg;
            q.y += 2.0 * o.y;
        }
        float bottom = q.y - o.y * (3.25 - (255.0 / 127.0) * TEX2(pxSmaaSearchUv(ee.gr, 0.5)).r);
        vec2 d = abs(floor(size.y * vec2(top, bottom) - vec2(pix.y) + vec2(0.5)));
        float e1 = TEX0(vec2(off0.x, top)).g;
        float e2 = TEX0(vec2(off0.x, bottom + o.y)).g;
        weights.ba = TEX1(pxSmaaAreaUv(sqrt(d), e1, e2)).rg;
        vec2 lr = step(d.xy, d.yx);
        vec2 r = rounding * lr / max(lr.x + lr.y, 1.0);
        vec2 factor = vec2(1.0);
        factor.x -= r.x * TEX0(vec2(t.x + o.x, top)).g;
        factor.x -= r.y * TEX0(vec2(t.x + o.x, bottom + o.y)).g;
        factor.y -= r.x * TEX0(vec2(t.x - 2.0 * o.x, top)).g;
        factor.y -= r.y * TEX0(vec2(t.x - 2.0 * o.x, bottom + o.y)).g;
        weights.ba *= clamp(factor, vec2(0.0), vec2(1.0));
    }
    RETURN(weights);
)";

// Neighbourhood blending: TEX0 = colour, TEX1 = blend weights.
inline const char* kSmaaBlend = R"(
    vec2 t = TUV(s);
    vec2 o = px.xy;
    vec4 a;
    a.x = TEX1(t + vec2(o.x, 0.0)).a;   // right
    a.y = TEX1(t + vec2(0.0, o.y)).g;   // bottom (texture space)
    a.w = TEX1(t).r;
    a.z = TEX1(t).b;
    if (dot(a, vec4(1.0)) < 0.00001) RETURN(TEX0(t));
    bool h = max(a.x, a.z) > max(a.y, a.w);
    vec4 blendingOffset = h ? vec4(a.x, 0.0, a.z, 0.0) : vec4(0.0, a.y, 0.0, a.w);
    vec2 blendingWeight = h ? a.xz : a.yw;
    blendingWeight /= dot(blendingWeight, vec2(1.0));
    vec4 bc = blendingOffset * vec4(o.x, o.y, -o.x, -o.y) + vec4(t, t);
    vec4 color = blendingWeight.x * TEX0(bc.xy) + blendingWeight.y * TEX0(bc.zw);
    RETURN(color);
)";

// ---- FSR 1 EASU (AMD FidelityFX, MIT; port of the FP32 path of ffx_fsr1.h).
// TEX0 = input (display-referred, input size). Output size = this pass.
// p0 = (input width, input height, output width, output height)
inline const char* kFsrHelpers = R"(
PF_FN vec2 pxFsrUv(vec2 pin, vec2 offs, vec2 rcpIn, float flip) {
    vec2 q = clamp((pin + offs + vec2(0.5)) * rcpIn, vec2(0.0), vec2(1.0));
    return flip > 0.5 ? vec2(q.x, 1.0 - q.y) : q;
}
#define PX_FSR_TAP(ox, oy) TEX0(pxFsrUv(fp, vec2(ox, oy), rcpIn, px.z)).rgb
PF_FN float pxFsrL(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
// Direction/length accumulation of one bilinear quadrant; returns (dir.x, dir.y, len).
PF_FN vec3 pxFsrSet(float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC, cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, 0.000001);
    float dirX = lD - lB;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    float ec = lE - lC, ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, 0.000001);
    float dirY = lE - lA;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    return vec3(dirX * w, dirY * w, (lenX + lenY) * w);
}
PF_FN vec4 pxFsrTapW(vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x);
    v *= len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    return vec4(c * w, w);
}
)";
inline const char* kFsrEasu = R"(
    // Output pixel -> input pixel position of 'f', in screen (y-up) pixels; the taps
    // apply the render-target flip once (flipping here too turned Metal upside down).
    vec2 rcpIn = vec2(1.0) / p0.xy;
    vec2 outPix = floor(s * p0.zw);
    vec2 pp = outPix * (p0.xy / p0.zw) + (0.5 * p0.xy / p0.zw - vec2(0.5));
    vec2 fp = floor(pp);
    pp -= fp;
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec3 b = PX_FSR_TAP(0.0, -1.0), c = PX_FSR_TAP(1.0, -1.0);
    vec3 e = PX_FSR_TAP(-1.0, 0.0), f = PX_FSR_TAP(0.0, 0.0);
    vec3 g = PX_FSR_TAP(1.0, 0.0), h = PX_FSR_TAP(2.0, 0.0);
    vec3 i = PX_FSR_TAP(-1.0, 1.0), j = PX_FSR_TAP(0.0, 1.0);
    vec3 k = PX_FSR_TAP(1.0, 1.0), l = PX_FSR_TAP(2.0, 1.0);
    vec3 n = PX_FSR_TAP(0.0, 2.0), o = PX_FSR_TAP(1.0, 2.0);
    float bL = pxFsrL(b), cL = pxFsrL(c), eL = pxFsrL(e), fL = pxFsrL(f), gL = pxFsrL(g), hL = pxFsrL(h);
    float iL = pxFsrL(i), jL = pxFsrL(j), kL = pxFsrL(k), lL = pxFsrL(l), nL = pxFsrL(n), oL = pxFsrL(o);
    vec3 acc = pxFsrSet((1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    acc += pxFsrSet(pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    acc += pxFsrSet((1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    acc += pxFsrSet(pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir = acc.xy;
    float len = acc.z;
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0 / 32768.0;
    dirR = zro ? 1.0 : 1.0 / sqrt(dirR);
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(max(abs(dir.x), abs(dir.y)), 0.000001);
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;
    vec3 min4 = min(min(f, g), min(j, k));
    vec3 max4 = max(max(f, g), max(j, k));
    vec4 sum = pxFsrTapW(vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    sum += pxFsrTapW(vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    sum += pxFsrTapW(vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    sum += pxFsrTapW(vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    sum += pxFsrTapW(vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    sum += pxFsrTapW(vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    sum += pxFsrTapW(vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    sum += pxFsrTapW(vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    sum += pxFsrTapW(vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    sum += pxFsrTapW(vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    sum += pxFsrTapW(vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    sum += pxFsrTapW(vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    vec3 pix = min(max4, max(min4, sum.rgb / sum.a));
    RETURN(vec4(pix, 1.0));
)";

// ---- FSR 1 RCAS: TEX0 = EASU output (same size). p0.x = sharpness (linear, exp2(-stops)).
inline const char* kFsrRcas = R"(
    vec2 t = TUV(s);
    vec2 o = px.xy;
    vec3 b = TEX0(t + vec2(0.0, -o.y)).rgb;
    vec3 d = TEX0(t + vec2(-o.x, 0.0)).rgb;
    vec3 e = TEX0(t).rgb;
    vec3 f = TEX0(t + vec2(o.x, 0.0)).rgb;
    vec3 h = TEX0(t + vec2(0.0, o.y)).rgb;
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g), dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g), fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    float range = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);
    nz = clamp(abs(nz) / max(range, 0.000001), 0.0, 1.0);
    nz = -0.5 * nz + 1.0;
    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(0.000001));
    vec3 hitMax = (vec3(1.0) - max(mx4, e)) / min(4.0 * mn4 - vec3(4.0), vec3(-0.000001));
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-(0.25 - 1.0 / 16.0), min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * p0.x;
    lobe *= nz;
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    vec3 c = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
    RETURN(vec4(c, 1.0));
)";

// ---- Histogram exposure metering. TEX0 = a log-luminance pyramid level (R = weighted
// log2 L, G = weight, both as block means). Output: 64 x 1, texel b = bin b.
// p0 = (level width, level height, min log2 L, max log2 L)
inline const char* kHistogram = R"(
    float bin = floor(s.x * 64.0);
    float lo = p0.z, span = max(p0.w - p0.z, 0.001);
    float weight = 0.0, logSum = 0.0;
    for (int y = 0; y < 64; y++) {
        if (float(y) >= p0.y) break;
        for (int x = 0; x < 64; x++) {
            if (float(x) >= p0.x) break;
            vec2 q = (vec2(float(x), float(y)) + vec2(0.5)) / p0.xy;
            vec2 v = TEX0(q).rg;
            if (v.y <= 0.0000001) continue;
            float logL = v.x / v.y;
            float b = clamp(floor((logL - lo) / span * 64.0), 0.0, 63.0);
            if (b == bin) { weight += v.y; logSum += v.x; }
        }
    }
    RETURN(vec4(weight, logSum, 0.0, 1.0));
)";

// TEX0 = 64 x 1 histogram. p0 = (low fraction, high fraction, -, -).
// Output 1 x 1: R = mean log2 L between the percentiles (times weight), G = weight;
// the same layout as the log-average meter, so kExposureAdapt reads either.
inline const char* kHistogramResolve = R"(
    float total = 0.0, allLog = 0.0;
    for (int b = 0; b < 64; b++) {
        vec2 v = TEX0(vec2((float(b) + 0.5) / 64.0, 0.5)).rg;
        total += v.x;
        allLog += v.y;
    }
    if (total <= 0.0000001) RETURN(vec4(0.0, 0.0, 0.0, 1.0));
    float lo = total * clamp(min(p0.x, p0.y), 0.0, 1.0);
    float hi = total * clamp(max(p0.x, p0.y), 0.0, 1.0);
    float cum = 0.0, sumLog = 0.0, sumW = 0.0;
    for (int b = 0; b < 64; b++) {
        vec2 v = TEX0(vec2((float(b) + 0.5) / 64.0, 0.5)).rg;
        if (v.x > 0.0) {
            float take = clamp(min(cum + v.x, hi) - max(cum, lo), 0.0, v.x);
            sumLog += take * (v.y / v.x);
            sumW += take;
        }
        cum += v.x;
    }
    if (sumW <= 0.0000001) RETURN(vec4(allLog / total, 1.0, 0.0, 1.0));
    RETURN(vec4(sumLog / sumW, 1.0, 0.0, 1.0));
)";

} // namespace aiverse::render::prisma
