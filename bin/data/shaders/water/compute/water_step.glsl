// Compute shader: water_step
// Combined slope reconstruction, Kurganov-Petrova flux, temporal derivative,
// and Euler/Runge-Kutta integration in a single dispatch.
//
// Adapted from SARndbox (Oliver Kreylos, 2012. GPL v2):
//   Water2SlopeAndFluxAndDerivativeShader.fs
//   Water2EulerStepShader.fs
//   Water2RungeKuttaStepShader.fs
//
// mode == 0: compute derivative + Euler predictor → writes derivative + q*
// mode == 1: compute derivative from q* + RK2 corrector → writes final q
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform image2D bathymetryImg;
layout(rgba32f, binding = 1) uniform image2D quantitySrc;      // q_n (current)
layout(rgba32f, binding = 2) uniform image2D quantityDst;      // output
layout(rgba32f, binding = 3) uniform image2D derivativeImg;    // dq/dt scratch
layout(rgba32f, binding = 4) uniform image2D quantityStar;     // q* (Euler prediction, read in mode 1)

uniform ivec2 gridSize;
uniform vec2  cellSize;
uniform float theta;
uniform float g;
uniform float epsilon;
uniform float stepSize;
uniform float attenuation;
uniform int   mode; // 0 = predictor, 1 = corrector
uniform int   fluidType; // 0 = water, 1 = lava

// ── Helper: minmod slope limiter ──────────────────────────────────

vec3 calcSlope(vec3 q0, vec3 q1, vec3 q2, float cs, float b0, float b1) {
    vec3 d01 = (q1 - q0) * (theta / cs);
    vec3 d02 = (q2 - q0) / (2.0 * cs);
    vec3 d12 = (q2 - q1) * (theta / cs);

    vec3 dMin = min(min(d01, d02), d12);
    vec3 dMax = max(max(d01, d02), d12);

    vec3 slope;
    slope.x = dMin.x > 0.0 ? dMin.x : dMax.x < 0.0 ? dMax.x : 0.0;
    slope.y = dMin.y > 0.0 ? dMin.y : dMax.y < 0.0 ? dMax.y : 0.0;
    slope.z = dMin.z > 0.0 ? dMin.z : dMax.z < 0.0 ? dMax.z : 0.0;

    if (q1.x - slope.x * cs * 0.5 < b0)
        slope.x = (q1.x - b0) / (cs * 0.5);
    if (q1.x + slope.x * cs * 0.5 < b1)
        slope.x = (b1 - q1.x) / (cs * 0.5);

    return slope;
}

// ── Helper: desingularized velocity ───────────────────────────────

vec2 calcUv(inout vec3 q, float h) {
    float h4 = h * h * h * h;
    vec2 uv = q.yz * (1.41421356237309 * h / sqrt(h4 + max(h4, epsilon)));
    q.yz = uv * h;
    return uv;
}

// ── Helper: Kurganov-Petrova x-flux ───────────────────────────────

float calcPartialFluxX(vec3 qe, vec3 qw, float bew, out vec3 fluxX) {
    float he = max(qe.x - bew, 0.0);
    float hw = max(qw.x - bew, 0.0);

    vec2 uve = calcUv(qe, he);
    vec2 uvw = calcUv(qw, hw);

    vec3 fe = vec3(qe.y, uve.x * qe.y + 0.5 * g * he * he, uve.y * qe.y);
    vec3 fw = vec3(qw.y, uvw.x * qw.y + 0.5 * g * hw * hw, uvw.y * qw.y);

    float sghe = sqrt(g * he);
    float sghw = sqrt(g * hw);
    float ae = min(min(uve.x - sghe, uvw.x - sghw), 0.0);
    float aw = max(max(uve.x + sghe, uvw.x + sghw), 0.0);

    fluxX = aw - ae != 0.0
        ? ((fe * aw - fw * ae) + (qw - qe) * (aw * ae)) / (aw - ae)
        : vec3(0.0);

    return 0.5 * cellSize.x / max(-ae, aw);
}

// ── Helper: Kurganov-Petrova y-flux ───────────────────────────────

float calcPartialFluxY(vec3 qn, vec3 qs, float bns, out vec3 fluxY) {
    float hn = max(qn.x - bns, 0.0);
    float hs = max(qs.x - bns, 0.0);

    vec2 uvn = calcUv(qn, hn);
    vec2 uvs = calcUv(qs, hs);

    vec3 fn = vec3(qn.z, uvn.x * qn.z, uvn.y * qn.z + 0.5 * g * hn * hn);
    vec3 fs = vec3(qs.z, uvs.x * qs.z, uvs.y * qs.z + 0.5 * g * hs * hs);

    float sghn = sqrt(g * hn);
    float sghs = sqrt(g * hs);
    float an = min(min(uvn.y - sghn, uvs.y - sghs), 0.0);
    float as = max(max(uvn.y + sghn, uvs.y + sghs), 0.0);

    fluxY = as - an != 0.0
        ? ((fn * as - fs * an) + (qs - qn) * (as * an)) / (as - an)
        : vec3(0.0);

    return 0.5 * cellSize.y / max(-an, as);
}

// ── Safe image load with clamping ─────────────────────────────────

vec4 loadClampedBathy(ivec2 coord) {
    coord = clamp(coord, ivec2(0), gridSize - 1);
    return imageLoad(bathymetryImg, coord);
}

vec4 loadClampedQ(ivec2 coord) {
    coord = clamp(coord, ivec2(0), gridSize - 1);
    // mode 0: read from quantitySrc (current state)
    // mode 1: read from quantityStar (Euler prediction)
    if (mode == 0)
        return imageLoad(quantitySrc, coord);
    else
        return imageLoad(quantityStar, coord);
}

// ── Main ──────────────────────────────────────────────────────────

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= gridSize.x || gid.y >= gridSize.y) return;

    // ── Fetch bathymetry stencil (vertex-corner values) ──
    float b00 = loadClampedBathy(gid + ivec2(-1, -1)).r;
    float b10 = loadClampedBathy(gid + ivec2( 0, -1)).r;
    float b01 = loadClampedBathy(gid + ivec2(-1,  0)).r;
    float b11 = imageLoad(bathymetryImg, gid).r;

    float b0 = (loadClampedBathy(gid + ivec2(-1, -2)).r +
                loadClampedBathy(gid + ivec2( 0, -2)).r) * 0.5;
    float b1 = (b00 + b10) * 0.5;
    float b2 = (loadClampedBathy(gid + ivec2(-2, -1)).r +
                loadClampedBathy(gid + ivec2(-2,  0)).r) * 0.5;
    float b3 = (b00 + b01) * 0.5;
    float b4 = (b10 + b11) * 0.5;
    float b5 = (loadClampedBathy(gid + ivec2( 1, -1)).r +
                loadClampedBathy(gid + ivec2( 1,  0)).r) * 0.5;
    float b6 = (b01 + b11) * 0.5;
    float b7 = (loadClampedBathy(gid + ivec2(-1,  1)).r +
                loadClampedBathy(gid + ivec2( 0,  1)).r) * 0.5;

    // ── Fetch quantity stencil ──
    vec3 q1 = loadClampedQ(gid + ivec2( 0, -1)).rgb;
    vec3 q3 = loadClampedQ(gid + ivec2(-1,  0)).rgb;
    vec3 q4 = loadClampedQ(gid).rgb;
    vec3 q5 = loadClampedQ(gid + ivec2( 1,  0)).rgb;
    vec3 q7 = loadClampedQ(gid + ivec2( 0,  1)).rgb;

    // ── Slope reconstruction + face values ──
    vec3 q1n = q1 + calcSlope(loadClampedQ(gid + ivec2(0, -2)).rgb, q1, q4, cellSize.y, b0, b1) * (cellSize.y * 0.5);
    vec3 q3e = q3 + calcSlope(loadClampedQ(gid + ivec2(-2, 0)).rgb, q3, q4, cellSize.x, b2, b3) * (cellSize.x * 0.5);

    vec3 q4x = calcSlope(q3, q4, q5, cellSize.x, b3, b4) * (cellSize.x * 0.5);
    vec3 q4w = q4 - q4x;
    vec3 q4e = q4 + q4x;

    vec3 q4y = calcSlope(q1, q4, q7, cellSize.y, b1, b6) * (cellSize.y * 0.5);
    vec3 q4s = q4 - q4y;
    vec3 q4n = q4 + q4y;

    vec3 q5w = q5 - calcSlope(q4, q5, loadClampedQ(gid + ivec2(2, 0)).rgb, cellSize.x, b4, b5) * (cellSize.x * 0.5);
    vec3 q7s = q7 - calcSlope(q4, q7, loadClampedQ(gid + ivec2(0, 2)).rgb, cellSize.y, b6, b7) * (cellSize.y * 0.5);

    // ── Compute fluxes ──
    vec3 fluxXw, fluxXe, fluxYs, fluxYn;
    calcPartialFluxX(q3e, q4w, b3, fluxXw);
    calcPartialFluxX(q4e, q5w, b4, fluxXe);
    calcPartialFluxY(q1n, q4s, b1, fluxYs);
    calcPartialFluxY(q4n, q7s, b6, fluxYn);

    // ── Water column height + gravitational source ──
    float h = max(q4.x - (b3 + b4) * 0.5, 0.0);
    vec3 source = vec3(0.0, -g * h * (b4 - b3) / cellSize.x, -g * h * (b6 - b1) / cellSize.y);

    // ── Temporal derivative ──
    vec3 dqdt = source - (fluxXe - fluxXw) / cellSize.x - (fluxYn - fluxYs) / cellSize.y;

    // Store derivative (used by RK2 corrector when mode == 1)
    imageStore(derivativeImg, gid, vec4(dqdt, 0.0));

    // ── Integration step ──
    vec3 qOrig = imageLoad(quantitySrc, gid).rgb; // always read from q_n

    vec3 newQ;
    if (mode == 0) {
        // Euler predictor: q* = q_n + dq/dt * dt
        newQ = q4 + dqdt * stepSize;
    } else {
        // RK2 corrector: q_new = 0.5 * (q_n + q* + dq*/dt * dt)
        vec3 qStar = imageLoad(quantityStar, gid).rgb;
        newQ = (qOrig + qStar + dqdt * stepSize) * 0.5;
    }

    // Momentum damping
    newQ.yz *= attenuation;

    imageStore(quantityDst, gid, vec4(newQ, 0.0));
}
