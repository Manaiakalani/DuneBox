// Compute shader: WaterRender
// Renders water/lava overlay to an RGBA image for compositing.
// Adapted from SARndbox WaterRenderingShader (Oliver Kreylos, 2014. GPL v2).
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform image2D quantityImg;
layout(rgba32f, binding = 1) uniform image2D bathymetryImg;
layout(rgba8,   binding = 2) uniform image2D outputImg;

uniform ivec2 gridSize;
uniform float waterOpacity;
uniform float time;
uniform int   fluidType; // 0 = water, 1 = lava
uniform float lavaTemp;  // 0.0 (cooled) to 1.0 (hot) — affects lava color

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= gridSize.x || gid.y >= gridSize.y) return;

    vec4 q = imageLoad(quantityImg, gid);

    // Cell-centered bathymetry
    ivec2 gm = ivec2(max(gid.x - 1, 0), max(gid.y - 1, 0));
    float b = (imageLoad(bathymetryImg, ivec2(gm.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(gid.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(gm.x, gid.y)).r +
               imageLoad(bathymetryImg, gid).r) * 0.25;

    float waterDepth = max(q.x - b, 0.0);

    if (waterDepth < 0.001) {
        imageStore(outputImg, gid, vec4(0.0));
        return;
    }

    // Compute simple normal from water surface height differences
    float wL = imageLoad(quantityImg, ivec2(max(gid.x - 1, 0), gid.y)).r;
    float wR = imageLoad(quantityImg, ivec2(min(gid.x + 1, gridSize.x - 1), gid.y)).r;
    float wD = imageLoad(quantityImg, ivec2(gid.x, max(gid.y - 1, 0))).r;
    float wU = imageLoad(quantityImg, ivec2(gid.x, min(gid.y + 1, gridSize.y - 1))).r;

    vec3 normal = normalize(vec3(wL - wR, wD - wU, 2.0));

    // Simple specular highlight from overhead light
    vec3 lightDir = normalize(vec3(0.3, 0.3, 1.0));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);

    vec4 result;

    if (fluidType == 0) {
        // Water: deep blue with depth-based opacity
        vec3 waterColor = vec3(0.1, 0.3, 0.7);
        float alpha = clamp(waterDepth * waterOpacity, 0.0, 0.85);
        result = vec4(waterColor + vec3(spec * 0.4), alpha);
    } else {
        // Lava: orange→red→black gradient based on temperature and depth
        float t = clamp(lavaTemp, 0.0, 1.0);
        float depthFactor = clamp(waterDepth * waterOpacity * 0.5, 0.0, 1.0);

        // Hot lava: bright orange-yellow. Cool lava: dark red-black.
        vec3 hotColor = vec3(1.0, 0.6, 0.1);
        vec3 coolColor = vec3(0.15, 0.02, 0.0);
        vec3 lavaColor = mix(coolColor, hotColor, t);

        // Emissive glow on hot lava
        float glow = t * 0.3 * (0.5 + 0.5 * sin(time * 2.0 + float(gid.x) * 0.1));
        lavaColor += vec3(glow, glow * 0.3, 0.0);

        float alpha = clamp(depthFactor + t * 0.3, 0.0, 0.95);
        result = vec4(lavaColor + vec3(spec * 0.15 * t), alpha);
    }

    imageStore(outputImg, gid, result);
}
