// Adapted from SARndbox Water2EulerStepShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// Forward Euler integration: q_new = q + dq/dt * dt, with momentum damping.
#version 150

uniform float stepSize;
uniform float attenuation;
uniform sampler2D quantitySampler;
uniform sampler2D derivativeSampler;

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec3 q = texture(quantitySampler, vTexCoord).rgb;
    vec3 qt = texture(derivativeSampler, vTexCoord).rgb;
    vec3 newQ = q + qt * stepSize;
    newQ.yz *= attenuation;
    fragColor = vec4(newQ, 0.0);
}
