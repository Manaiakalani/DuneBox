// Compute shader: Erosion
// Erosion and sedimentation, after the sediment transport in
// danigeos/sARndbox. Same model as adapted/Erosion.frag.
//
// sedimentImg: R = bed change (negative = eroded, positive = deposited)
//              G = suspended sediment carried by the water
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform readonly image2D quantityImg;
layout(rgba32f, binding = 1) uniform readonly image2D bathymetryImg;
layout(rgba32f, binding = 2) uniform readonly image2D oldBathymetryImg;
layout(rgba32f, binding = 3) uniform readonly image2D sedimentIn;
layout(rgba32f, binding = 4) uniform writeonly image2D sedimentOut;

uniform ivec2 gridSize;
uniform float dt;
uniform float cellSize;
uniform float capacity;
uniform float erodeRate;
uniform float depositRate;
uniform float fade;
uniform float resetDelta;
uniform float maxBed;

ivec2 clampCell(ivec2 p) {
    return clamp(p, ivec2(0), gridSize - 1);
}

float cellBathymetry(bool old, ivec2 gid) {
    ivec2 gm = ivec2(max(gid.x - 1, 0), max(gid.y - 1, 0));
    if (old) {
        return (imageLoad(oldBathymetryImg, gm).r +
                imageLoad(oldBathymetryImg, ivec2(gid.x, gm.y)).r +
                imageLoad(oldBathymetryImg, ivec2(gm.x, gid.y)).r +
                imageLoad(oldBathymetryImg, gid).r) * 0.25;
    }
    return (imageLoad(bathymetryImg, gm).r +
            imageLoad(bathymetryImg, ivec2(gid.x, gm.y)).r +
            imageLoad(bathymetryImg, ivec2(gm.x, gid.y)).r +
            imageLoad(bathymetryImg, gid).r) * 0.25;
}

// Depth-averaged velocity (cells per second) and water depth of a cell.
vec3 flow(ivec2 p) {
    p = clampCell(p);
    vec4 q = imageLoad(quantityImg, p);
    float h = max(q.x - cellBathymetry(false, p), 0.0);
    vec2 v = h > 0.001 ? q.yz / h / cellSize : vec2(0.0);
    return vec3(clamp(v, -4.0, 4.0), h);
}

// Upwind sediment flux across the face between a and its neighbour b,
// positive in the a -> b direction.
float faceFlux(ivec2 a, ivec2 b, float va, float vb) {
    float vf = 0.5 * (va + vb);
    float source = vf > 0.0 ? imageLoad(sedimentIn, clampCell(a)).g
                            : imageLoad(sedimentIn, clampCell(b)).g;
    return vf * source;
}

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= gridSize.x || gid.y >= gridSize.y) return;

    if (abs(cellBathymetry(false, gid) - cellBathymetry(true, gid)) > resetDelta) {
        imageStore(sedimentOut, gid, vec4(0.0));
        return;
    }

    vec3 c = flow(gid);
    vec3 w = flow(gid + ivec2(-1, 0));
    vec3 e = flow(gid + ivec2(1, 0));
    vec3 s = flow(gid + ivec2(0, -1));
    vec3 n = flow(gid + ivec2(0, 1));

    // Walls: nothing crosses the grid edge
    float fw = gid.x > 0               ? faceFlux(gid + ivec2(-1, 0), gid, w.x, c.x) : 0.0;
    float fe = gid.x < gridSize.x - 1  ? faceFlux(gid, gid + ivec2(1, 0), c.x, e.x)  : 0.0;
    float fs = gid.y > 0               ? faceFlux(gid + ivec2(0, -1), gid, s.y, c.y) : 0.0;
    float fn = gid.y < gridSize.y - 1  ? faceFlux(gid, gid + ivec2(0, 1), c.y, n.y)  : 0.0;

    vec4 sed = imageLoad(sedimentIn, gid);
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
    imageStore(sedimentOut, gid, vec4(bed, carried, 0.0, 1.0));
}
