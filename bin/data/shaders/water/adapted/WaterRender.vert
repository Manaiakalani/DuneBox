// Adapted from SARndbox WaterRenderingShader.vs
// Original: Oliver Kreylos, 2014. GPL v2.
// Simplified for 2D overlay rendering (no 3D mesh / Gouraud lighting).
// Passes through water depth as alpha for transparency.
#version 150

uniform sampler2D quantitySampler;
uniform sampler2D bathymetrySampler;
uniform vec2 texelSize;

in vec4 position;
in vec2 texcoord;

out vec2 vTexCoord;
out float waterDepth;

void main() {
    vTexCoord = texcoord;

    // Sample water surface height and bathymetry
    float w = texture(quantitySampler, texcoord).r;
    float b = (texture(bathymetrySampler, texcoord + vec2(-texelSize.x, -texelSize.y)).r +
               texture(bathymetrySampler, texcoord + vec2(0.0, -texelSize.y)).r +
               texture(bathymetrySampler, texcoord + vec2(-texelSize.x, 0.0)).r +
               texture(bathymetrySampler, texcoord).r) * 0.25;

    waterDepth = max(w - b, 0.0);
    gl_Position = position;
}
