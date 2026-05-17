// Passthrough vertex shader for fullscreen quad simulation passes.
// Adapted from SARndbox dynamically-generated vertex shader.
// Original: Oliver Kreylos, 2012. GPL v2.
#version 150

in vec4 position;
in vec2 texcoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = texcoord;
    gl_Position = position;
}
