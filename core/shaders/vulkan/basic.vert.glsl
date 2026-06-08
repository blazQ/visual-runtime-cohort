#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 frag_color;

layout(binding = 0) uniform FrameUniforms {
    mat4 matrix;
} frame_uniforms;

void main() {
    gl_Position = frame_uniforms.matrix * vec4(in_position, 0.0, 1.0);
    frag_color = in_color;
}
