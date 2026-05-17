// Adapted from SARndbox WaterRenderingShader.fs + SurfaceAddWaterColor.fs
// Original: Oliver Kreylos, 2014. GPL v2.
// Simplified water visualization as a blue-tinted overlay.
// Uses water depth for opacity and adds simple specular highlight.
#version 150

uniform sampler2D quantitySampler;
uniform sampler2D bathymetrySampler;
uniform vec2 texelSize;
uniform float waterOpacity;
uniform float time;

in vec2 vTexCoord;
in float waterDepth;
out vec4 fragColor;

void main() {
    if (waterDepth < 0.001) discard;

    // Compute simple normal from water surface height differences
    float wL = texture(quantitySampler, vTexCoord + vec2(-texelSize.x, 0.0)).r;
    float wR = texture(quantitySampler, vTexCoord + vec2(texelSize.x, 0.0)).r;
    float wD = texture(quantitySampler, vTexCoord + vec2(0.0, -texelSize.y)).r;
    float wU = texture(quantitySampler, vTexCoord + vec2(0.0, texelSize.y)).r;

    vec3 normal = normalize(vec3(wL - wR, wD - wU, 2.0));

    // Simple specular highlight from overhead light
    vec3 lightDir = normalize(vec3(0.3, 0.3, 1.0));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);

    // Water color: deep blue with depth-based opacity
    vec3 waterColor = vec3(0.1, 0.3, 0.7);
    float alpha = clamp(waterDepth * waterOpacity, 0.0, 0.85);

    fragColor = vec4(waterColor + vec3(spec * 0.4), alpha);
}
