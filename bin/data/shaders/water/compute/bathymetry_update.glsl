// Compute shader: BathymetryUpdate
// Adjusts water surface height when terrain (bathymetry) changes.
// Adapted from SARndbox Water2BathymetryUpdateShader.fs (Oliver Kreylos, 2012. GPL v2).
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform image2D oldBathymetryImg;
layout(rgba32f, binding = 1) uniform image2D newBathymetryImg;
layout(rgba32f, binding = 2) uniform image2D quantityIn;
layout(rgba32f, binding = 3) uniform image2D quantityOut;

uniform ivec2 gridSize;

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= gridSize.x || gid.y >= gridSize.y) return;

    // Average 4 surrounding vertex corners for cell-centered bathymetry.
    // Clamp neighbor reads to valid range.
    ivec2 gm = ivec2(max(gid.x - 1, 0), max(gid.y - 1, 0));

    float bOld = (imageLoad(oldBathymetryImg, ivec2(gm.x, gm.y)).r +
                  imageLoad(oldBathymetryImg, ivec2(gid.x, gm.y)).r +
                  imageLoad(oldBathymetryImg, ivec2(gm.x, gid.y)).r +
                  imageLoad(oldBathymetryImg, gid).r) * 0.25;

    float bNew = (imageLoad(newBathymetryImg, ivec2(gm.x, gm.y)).r +
                  imageLoad(newBathymetryImg, ivec2(gid.x, gm.y)).r +
                  imageLoad(newBathymetryImg, ivec2(gm.x, gid.y)).r +
                  imageLoad(newBathymetryImg, gid).r) * 0.25;

    vec4 q = imageLoad(quantityIn, gid);

    // Adjust water surface: preserve water column height relative to new terrain
    q.x = max(q.x - bOld, 0.0) + bNew;

    imageStore(quantityOut, gid, q);
}
