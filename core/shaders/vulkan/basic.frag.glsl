#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

layout(location = 1) in vec2 frag_local;
layout(location = 2) flat in uint frag_shape_kind;
layout(location = 3) in vec2 frag_uv;
layout(location = 4) flat in uint frag_texture_index;

layout(binding = 2) uniform sampler2D textures[];

const uint kKindRectangle    = 1u;
const uint kKindCircle       = 2u;
const uint kKindSelectionBox = 3u;
const uint kKindRoundedBox   = 4u;

const uint kNoTexture = 0xFFFFFFFFu;
const float kSelectionBorderThickness = 0.03;

const float kRoundedBoxRadius = 0.2;
const float kCircleRadius = 0.5;
const float kBoxHalfSize = 0.5;

// Signed distance functions (ref. Inigo Quilez 2D SDF). frag_local spans [-0.5, 0.5];
// negative inside the shape, zero on the boundary, positive outside.
float sd_circle(vec2 p, float radius) {
    return length(p) - radius;
}

float sd_box(vec2 p, vec2 half_size) {
    vec2 d = abs(p) - half_size;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sd_rounded_box(in vec2 p, in vec2 half_size, in vec4 roundness){
    roundness.xy = (p.x>0.0)?roundness.xy : roundness.zw;
    roundness.x = (p.y > 0.0)?roundness.x : roundness.y;
    vec2 q = abs(p) - half_size + roundness.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - roundness.x;
}

// Signed distance to this kind's boundary.
// Basically where new shapes are added.
float shape_distance(uint kind) {
    if (kind == kKindCircle) {
        return sd_circle(frag_local, kCircleRadius); // fixed radius
    }
    
    if (kind == kKindRoundedBox) {
        return sd_rounded_box(frag_local, vec2(0.5), vec4(kRoundedBoxRadius));
    }
    return sd_box(frag_local, vec2(kBoxHalfSize)); // rectangle and selection box
}

// Sample the bound texture if the drawable has one, else use the flat color.
vec4 shape_fill() {
    if (frag_texture_index != kNoTexture) {
        return texture(textures[nonuniformEXT(frag_texture_index)], frag_uv);
    }
    return frag_color;
}

void main() {
    float d = shape_distance(frag_shape_kind);

    // Selection Box outline.
    if (frag_shape_kind == kKindSelectionBox) {
        if (d < -kSelectionBorderThickness || d > 0.0) discard;
        out_color = frag_color;
        return;
    }

    // Drop everything outside the boundary.
    if (d > 0.0) discard;
    out_color = shape_fill();
}
