// Adapted from SARndbox Water2WaterAddShader.vs
// Original: Oliver Kreylos, 2012-2014. GPL v2.
// Vertex shader for rain/drain geometry.
// Simplified: uses uniform waterAmount instead of per-vertex attribute.
// Vertices are supplied in NDC [-1,1] so we bypass the OF MVP matrix.
#version 150

uniform float stepSize;
uniform float waterAmount;

in vec4 position;

out float scaledWaterAmount;

void main() {
    scaledWaterAmount = waterAmount * stepSize;
    gl_Position = position;
}
