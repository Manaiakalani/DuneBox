// Adapted from SARndbox Water2WaterAddShader.fs
// Original: Oliver Kreylos, 2012-2014. GPL v2.
// Outputs scaled water amount for additive blending.
#version 150

in float scaledWaterAmount;
out vec4 fragColor;

void main() {
    fragColor = vec4(scaledWaterAmount);
}
