// Compute shader: WaterAdd + WaterUpdate (combined)
// Applies rain/drain additions and evaporation to the water quantity field.
// Adapted from SARndbox Water2WaterAddShader / Water2WaterUpdateShader
// (Oliver Kreylos, 2012-2014. GPL v2).
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform image2D bathymetryImg;
layout(rgba32f, binding = 1) uniform image2D quantityIn;
layout(rgba32f, binding = 2) uniform image2D quantityOut;

uniform ivec2 gridSize;
uniform float stepSize;

// Rain/drain sources (up to 32 simultaneous touch points)
#define MAX_SOURCES 32
uniform int   numSources;
uniform vec4  sources[MAX_SOURCES]; // (x, y, radius, amount) in grid coords

// Evaporation rate per second (0 = disabled)
uniform float evaporationRate;

// Fluid type: 0 = water, 1 = lava
uniform int fluidType;

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= gridSize.x || gid.y >= gridSize.y) return;

    // Cell-centered bathymetry
    ivec2 gm = ivec2(max(gid.x - 1, 0), max(gid.y - 1, 0));

    float b = (imageLoad(bathymetryImg, ivec2(gm.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(gid.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(gm.x, gid.y)).r +
               imageLoad(bathymetryImg, gid).r) * 0.25;

    vec4 q = imageLoad(quantityIn, gid);
    float hOld = q.x - b;

    // Accumulate water from all active sources
    float addedWater = 0.0;
    vec2 pos = vec2(float(gid.x), float(gid.y));

    for (int i = 0; i < numSources && i < MAX_SOURCES; i++) {
        vec2 center = sources[i].xy;
        float radius = sources[i].z;
        float amount = sources[i].w;

        float dist = length(pos - center);
        if (dist < radius) {
            // Smooth falloff within the disk
            float t = 1.0 - (dist / radius);
            addedWater += amount * t * stepSize;
        }
    }

    // Apply evaporation (only for water, not lava)
    float evap = 0.0;
    if (fluidType == 0) {
        evap = evaporationRate * stepSize;
    }

    float hNew = max(hOld + addedWater - evap, 0.0);

    q.x = hNew + b;
    // Scale momentum if water is draining
    q.yz = hNew == 0.0 ? vec2(0.0) : (hNew < hOld ? q.yz * (hNew / hOld) : q.yz);

    imageStore(quantityOut, gid, q);
}
