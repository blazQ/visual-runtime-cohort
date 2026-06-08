#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

struct FrameUniforms {
    float4x4 matrix;
};

struct DrawableUniforms {
    float4x4 model_transform;
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant FrameUniforms& frame_uniforms [[buffer(1)]],
                             constant DrawableUniforms& drawable_uniforms [[buffer(2)]]) {
    VertexOut out;
    out.position = frame_uniforms.matrix * drawable_uniforms.model_transform * float4(in.position, 0.0, 1.0);
    out.color = drawable_uniforms.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}
