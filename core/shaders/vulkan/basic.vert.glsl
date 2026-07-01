#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_position;

layout(location = 0) out vec4 frag_color;

layout(binding = 0) uniform FrameUniforms {
    mat4 matrix;
} frame_uniforms;

layout(location = 1) out vec2 frag_local;

layout(location = 2) flat out uint frag_shape_kind;

struct DrawableUniforms {
    mat4 model_transform;
    vec4 color;
    uint kind;
};

layout(binding = 1) readonly buffer DrawableState {
    DrawableUniforms drawable_uniforms;
} drawable_states[];

layout(push_constant) uniform DrawPush {
    uint drawable_index;
} draw_push;

void main() {
    DrawableUniforms drawable = drawable_states[nonuniformEXT(draw_push.drawable_index)].drawable_uniforms;
    gl_Position = frame_uniforms.matrix * drawable.model_transform * vec4(in_position, 0.0, 1.0);
    frag_color = drawable.color;
    frag_shape_kind = drawable.kind;
    frag_local = in_position;
}
