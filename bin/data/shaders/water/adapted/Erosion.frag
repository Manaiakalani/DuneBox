// Erosion and sedimentation, after the sediment transport in
// danigeos/sARndbox. Flowing water picks up sediment up to a capacity that
// grows with speed and depth, carries it downstream (upwind flux, so it
// reaches still water), and drops it where the flow slows or dries out.
//
// The real sand cannot move, so the result is a separate bed-change layer
// that is only drawn. It fades over time and is wiped wherever the sand
// itself changes.
//
// sedimentSampler: R = bed change (negative = eroded, positive = deposited)
//                  G = suspended sediment carried by the water
#version 150

uniform sampler2D quantitySampler;
uniform sampler2D bathymetrySampler;
uniform sampler2D oldBathymetrySampler;
uniform sampler2D sedimentSampler;
uniform float dt;
uniform float cellSize;
uniform float capacity;     // sediment a unit of flow can carry
uniform float erodeRate;    // per second
uniform float depositRate;  // per second
uniform float fade;         // per second
uniform float resetDelta;   // terrain change that wipes the layer
uniform float maxBed;

in vec2 vTexCoord;
out vec4 fragColor;

ivec2 gridSize;

ivec2 clampCell(ivec2 p) {
    return clamp(p, ivec2(0), gridSize - 1);
}

float cellBathymetry(sampler2D s, ivec2 p) {
    ivec2 pm = max(p - 1, ivec2(0));
    return (texelFetch(s, pm, 0).r +
            texelFetch(s, ivec2(p.x, pm.y), 0).r +
            texelFetch(s, ivec2(pm.x, p.y), 0).r +
            texelFetch(s, p, 0).r) * 0.25;
}

// Depth-averaged velocity (cells per second) and water depth of a cell.
vec3 flow(ivec2 p) {
    p = clampCell(p);
    vec3 q = texelFetch(quantitySampler, p, 0).rgb;
    float h = max(q.x - cellBathymetry(bathymetrySampler, p), 0.0);
    vec2 v = h > 0.001 ? q.yz / h / cellSize : vec2(0.0);
    return vec3(clamp(v, -4.0, 4.0), h);
}

// Upwind sediment flux across the face between a and its neighbour b,
// positive in the a -> b direction.
float faceFlux(ivec2 a, ivec2 b, float va, float vb) {
    float vf = 0.5 * (va + vb);
    float source = vf > 0.0 ? texelFetch(sedimentSampler, clampCell(a), 0).g
                            : texelFetch(sedimentSampler, clampCell(b), 0).g;
    return vf * source;
}

void main() {
    gridSize = textureSize(quantitySampler, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);

    if (abs(cellBathymetry(bathymetrySampler, p) - cellBathymetry(oldBathymetrySampler, p)) > resetDelta) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 c = flow(p);
    vec3 w = flow(p + ivec2(-1, 0));
    vec3 e = flow(p + ivec2(1, 0));
    vec3 s = flow(p + ivec2(0, -1));
    vec3 n = flow(p + ivec2(0, 1));

    // Walls: nothing crosses the grid edge
    float fw = p.x > 0              ? faceFlux(p + ivec2(-1, 0), p, w.x, c.x) : 0.0;
    float fe = p.x < gridSize.x - 1 ? faceFlux(p, p + ivec2(1, 0), c.x, e.x)  : 0.0;
    float fs = p.y > 0              ? faceFlux(p + ivec2(0, -1), p, s.y, c.y) : 0.0;
    float fn = p.y < gridSize.y - 1 ? faceFlux(p, p + ivec2(0, 1), c.y, n.y)  : 0.0;

    vec4 sed = texelFetch(sedimentSampler, p, 0);
    float bed = sed.r;
    float carried = max(sed.g + dt * (fw - fe + fs - fn), 0.0);
    float h = c.z;

    if (h <= 0.001) {
        bed += carried;
        carried = 0.0;
    } else {
        float cap = capacity * length(c.xy) * min(h, 0.1);
        if (carried < cap) {
            float take = min((cap - carried) * erodeRate * dt, bed + maxBed);
            bed -= take;
            carried += take;
        } else {
            float drop = (carried - cap) * depositRate * dt;
            bed += drop;
            carried -= drop;
        }
    }

    bed = clamp(bed * (1.0 - fade * dt), -maxBed, maxBed);
    fragColor = vec4(bed, carried, 0.0, 1.0);
}
