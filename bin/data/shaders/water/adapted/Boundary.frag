// Adapted from SARndbox Water2BoundaryShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// Enforces dry boundary conditions at domain edges.
#version 150

uniform sampler2D bathymetrySampler;
uniform vec2 texelSize;

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec2 fc = vTexCoord;

    float b = (texture(bathymetrySampler, fc + vec2(-texelSize.x, -texelSize.y)).r +
               texture(bathymetrySampler, fc + vec2(0.0, -texelSize.y)).r +
               texture(bathymetrySampler, fc + vec2(-texelSize.x, 0.0)).r +
               texture(bathymetrySampler, fc).r) * 0.25;

    fragColor = vec4(b, 0.0, 0.0, 0.0);
}
