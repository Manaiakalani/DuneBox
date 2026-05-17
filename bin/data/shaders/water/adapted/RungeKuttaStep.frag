// Adapted from SARndbox Water2RungeKuttaStepShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// RK2 corrector: q_final = 0.5 * (q0 + q* + dq*/dt * dt)
#version 150

uniform float stepSize;
uniform float attenuation;
uniform sampler2D quantitySampler;
uniform sampler2D quantityStarSampler;
uniform sampler2D derivativeSampler;

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec3 q = texture(quantitySampler, vTexCoord).rgb;
    vec3 qStar = texture(quantityStarSampler, vTexCoord).rgb;
    vec3 qt = texture(derivativeSampler, vTexCoord).rgb;
    vec3 newQ = (q + qStar + qt * stepSize) * 0.5;
    newQ.yz *= attenuation;
    fragColor = vec4(newQ, 0.0);
}
