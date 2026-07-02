#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

layout(location = 1) in vec2 frag_local;
layout(location = 2) flat in uint frag_shape_kind;
layout(location = 3) in vec2 frag_uv;
layout(location = 4) flat in uint frag_texture_index;

layout(binding = 2) uniform sampler2D textures[];

void main() {
    
    if(frag_shape_kind == 2u && length(frag_local) > 0.5){
        discard;
    }
    
    if (frag_texture_index != 0xFFFFFFFFu) {
        out_color = texture(textures[nonuniformEXT(frag_texture_index)], frag_uv);
    } else {
        out_color = frag_color;
    }
}
