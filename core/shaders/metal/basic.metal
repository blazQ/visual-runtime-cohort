#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

struct FrameUniforms {
    float4x4 matrix;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant FrameUniforms& frame_uniforms [[buffer(1)]]) {
    VertexOut out;
    out.position = frame_uniforms.matrix * float4(in.position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}
