#version 450

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

layout(location = 1) in vec2 frag_local;
layout(location = 2) flat in uint frag_shape_kind;

void main() {
    
    if(frag_shape_kind == 2u && length(frag_local) > 0.5){
        discard;
    }
    
    out_color = frag_color;
}
