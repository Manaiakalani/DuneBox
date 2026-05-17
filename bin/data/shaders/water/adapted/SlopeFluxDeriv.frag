// Adapted from SARndbox Water2SlopeAndFluxAndDerivativeShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// Combined slope estimation, Kurganov-Petrova flux, and temporal derivative.
// This is the PRIMARY derivative computation shader.
//
// Since OF core profile doesn't support MRT via gl_FragData, we output
// only the derivative (output 0). The max step size (output 1) is not
// used because this POC uses a fixed timestep.
#version 150

uniform vec2 cellSize;
uniform float theta;
uniform float g;
uniform float epsilon;
uniform sampler2D bathymetrySampler;
uniform sampler2D quantitySampler;
uniform vec2 texelSize; // 1.0 / textureSize

in vec2 vTexCoord;
out vec4 fragColor;

vec3 calcSlope(in vec3 q0, in vec3 q1, in vec3 q2, in float cs, in float b0, in float b1) {
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

vec2 calcUv(inout vec3 q, in float h) {
    float h4 = h * h * h * h;
    vec2 uv = q.yz * (1.41421356237309 * h / sqrt(h4 + max(h4, epsilon)));
    q.yz = uv * h;
    return uv;
}

float calcPartialFluxX(in vec3 qe, in vec3 qw, in float bew, out vec3 fluxX) {
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

    fluxX = aw - ae != 0.0 ? ((fe * aw - fw * ae) + (qw - qe) * (aw * ae)) / (aw - ae) : vec3(0.0);

    return 0.5 * cellSize.x / max(-ae, aw);
}

float calcPartialFluxY(in vec3 qn, in vec3 qs, in float bns, out vec3 fluxY) {
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

    fluxY = as - an != 0.0 ? ((fn * as - fs * an) + (qs - qn) * (as * an)) / (as - an) : vec3(0.0);

    return 0.5 * cellSize.y / max(-an, as);
}

void main() {
    vec2 fc = vTexCoord;
    vec2 ts = texelSize;

    // Fetch bathymetry vertex corners (5×5 stencil)
    float b00 = texture(bathymetrySampler, fc + vec2(-ts.x, -ts.y)).r;
    float b10 = texture(bathymetrySampler, fc + vec2(0.0, -ts.y)).r;
    float b01 = texture(bathymetrySampler, fc + vec2(-ts.x, 0.0)).r;
    float b11 = texture(bathymetrySampler, fc).r;

    float b0 = (texture(bathymetrySampler, fc + vec2(-ts.x, -2.0 * ts.y)).r +
                texture(bathymetrySampler, fc + vec2(0.0, -2.0 * ts.y)).r) * 0.5;
    float b1 = (b00 + b10) * 0.5;
    float b2 = (texture(bathymetrySampler, fc + vec2(-2.0 * ts.x, -ts.y)).r +
                texture(bathymetrySampler, fc + vec2(-2.0 * ts.x, 0.0)).r) * 0.5;
    float b3 = (b00 + b01) * 0.5;
    float b4 = (b10 + b11) * 0.5;
    float b5 = (texture(bathymetrySampler, fc + vec2(ts.x, -ts.y)).r +
                texture(bathymetrySampler, fc + vec2(ts.x, 0.0)).r) * 0.5;
    float b6 = (b01 + b11) * 0.5;
    float b7 = (texture(bathymetrySampler, fc + vec2(-ts.x, ts.y)).r +
                texture(bathymetrySampler, fc + vec2(0.0, ts.y)).r) * 0.5;

    // Fetch quantity stencil
    vec3 q1 = texture(quantitySampler, fc + vec2(0.0, -ts.y)).rgb;
    vec3 q3 = texture(quantitySampler, fc + vec2(-ts.x, 0.0)).rgb;
    vec3 q4 = texture(quantitySampler, fc).rgb;
    vec3 q5 = texture(quantitySampler, fc + vec2(ts.x, 0.0)).rgb;
    vec3 q7 = texture(quantitySampler, fc + vec2(0.0, ts.y)).rgb;

    // One-sided reconstructed quantities at cell faces
    vec3 q1n = q1 + calcSlope(texture(quantitySampler, fc + vec2(0.0, -2.0 * ts.y)).rgb, q1, q4, cellSize.y, b0, b1) * (cellSize.y * 0.5);
    vec3 q3e = q3 + calcSlope(texture(quantitySampler, fc + vec2(-2.0 * ts.x, 0.0)).rgb, q3, q4, cellSize.x, b2, b3) * (cellSize.x * 0.5);

    vec3 q4x = calcSlope(q3, q4, q5, cellSize.x, b3, b4) * (cellSize.x * 0.5);
    vec3 q4w = q4 - q4x;
    vec3 q4e = q4 + q4x;

    vec3 q4y = calcSlope(q1, q4, q7, cellSize.y, b1, b6) * (cellSize.y * 0.5);
    vec3 q4s = q4 - q4y;
    vec3 q4n = q4 + q4y;

    vec3 q5w = q5 - calcSlope(q4, q5, texture(quantitySampler, fc + vec2(2.0 * ts.x, 0.0)).rgb, cellSize.x, b4, b5) * (cellSize.x * 0.5);
    vec3 q7s = q7 - calcSlope(q4, q7, texture(quantitySampler, fc + vec2(0.0, 2.0 * ts.y)).rgb, cellSize.y, b6, b7) * (cellSize.y * 0.5);

    // Compute partial fluxes across all 4 faces
    vec3 fluxXw, fluxXe, fluxYs, fluxYn;
    calcPartialFluxX(q3e, q4w, b3, fluxXw);
    calcPartialFluxX(q4e, q5w, b4, fluxXe);
    calcPartialFluxY(q1n, q4s, b1, fluxYs);
    calcPartialFluxY(q4n, q7s, b6, fluxYn);

    // Water column height at cell center
    float h = max(q4.x - (b3 + b4) * 0.5, 0.0);

    // Gravitational source terms
    vec3 source = vec3(0.0, -g * h * (b4 - b3) / cellSize.x, -g * h * (b6 - b1) / cellSize.y);

    // Temporal derivative
    fragColor = vec4(source - (fluxXe - fluxXw) / cellSize.x - (fluxYn - fluxYs) / cellSize.y, 0.0);
}
