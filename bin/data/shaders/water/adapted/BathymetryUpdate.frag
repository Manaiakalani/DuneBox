// Adapted from SARndbox Water2BathymetryUpdateShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// Adjusts water surface height after bathymetry change.
#version 150

uniform sampler2D oldBathymetrySampler;
uniform sampler2D newBathymetrySampler;
uniform sampler2D quantitySampler;
uniform vec2 texelSize; // 1.0 / textureSize

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec2 fc = vTexCoord;

    // Average 4 surrounding vertex corners for cell-centered bathymetry
    float bOld = (texture(oldBathymetrySampler, fc + vec2(-texelSize.x, -texelSize.y)).r +
                  texture(oldBathymetrySampler, fc + vec2(0.0, -texelSize.y)).r +
                  texture(oldBathymetrySampler, fc + vec2(-texelSize.x, 0.0)).r +
                  texture(oldBathymetrySampler, fc).r) * 0.25;

    float bNew = (texture(newBathymetrySampler, fc + vec2(-texelSize.x, -texelSize.y)).r +
                  texture(newBathymetrySampler, fc + vec2(0.0, -texelSize.y)).r +
                  texture(newBathymetrySampler, fc + vec2(-texelSize.x, 0.0)).r +
                  texture(newBathymetrySampler, fc).r) * 0.25;

    vec3 q = texture(quantitySampler, fc).rgb;

    fragColor = vec4(max(q.x - bOld, 0.0) + bNew, q.yz, 0.0);
}
