// Compute shader: Boundary
// Enforces dry boundary conditions at domain edges.
// Sets edge cells to bathymetry height with zero momentum.
// Adapted from SARndbox Water2BoundaryShader.fs (Oliver Kreylos, 2012. GPL v2).
#version 430

layout(local_size_x = 256) in;

layout(rgba32f, binding = 0) uniform image2D bathymetryImg;
layout(rgba32f, binding = 1) uniform image2D quantityImg;

uniform ivec2 gridSize;

void main() {
    uint idx = gl_GlobalInvocationID.x;

    // Total boundary cells = 2 * width + 2 * height - 4 corners
    // We process in segments: bottom row, top row, left column, right column
    int w = gridSize.x;
    int h = gridSize.y;

    ivec2 gid;
    bool valid = false;

    if (idx < uint(w)) {
        // Bottom row
        gid = ivec2(int(idx), 0);
        valid = true;
    } else if (idx < uint(2 * w)) {
        // Top row
        gid = ivec2(int(idx) - w, h - 1);
        valid = true;
    } else if (idx < uint(2 * w + h)) {
        // Left column (excluding corners already processed)
        int row = int(idx) - 2 * w;
        if (row >= 0 && row < h) {
            gid = ivec2(0, row);
            valid = true;
        }
    } else if (idx < uint(2 * w + 2 * h)) {
        // Right column
        int row = int(idx) - 2 * w - h;
        if (row >= 0 && row < h) {
            gid = ivec2(w - 1, row);
            valid = true;
        }
    }

    if (!valid) return;

    // Average 4 surrounding vertex corners for cell-centered bathymetry
    ivec2 gm = ivec2(max(gid.x - 1, 0), max(gid.y - 1, 0));
    ivec2 clamped = min(gid, gridSize - 1);

    float b = (imageLoad(bathymetryImg, ivec2(gm.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(clamped.x, gm.y)).r +
               imageLoad(bathymetryImg, ivec2(gm.x, clamped.y)).r +
               imageLoad(bathymetryImg, clamped).r) * 0.25;

    // Set to dry: water surface = bathymetry, zero momentum
    imageStore(quantityImg, gid, vec4(b, 0.0, 0.0, 0.0));
}
