struct CanvasPush {
    float2 viewport;
    float2 phase;
    float spacing;
    float grid_opacity;
    float4 rect;
    float4 color;
    float2 group_style;
};

[vk::push_constant]
uniform CanvasPush push_data;

[[vk::binding(0, 0)]] Texture2D<float4> picture_texture;
[[vk::binding(1, 0)]] SamplerState picture_sampler;

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput rectangle_vertex(uint index) {
    float2 corner = float2((index == 1 || index == 2 || index == 4) ? 1.0f : 0.0f, (index == 2 || index == 4 || index == 5) ? 1.0f : 0.0f);
    float2 pixel = push_data.rect.xy + corner * push_data.rect.zw;
    VertexOutput output;
    output.position = float4(pixel * 2.0f / push_data.viewport - 1.0f, 0.0f, 1.0f);
    output.uv = corner;
    return output;
}

[shader("vertex")]
VertexOutput grid_vertex(uint index : SV_VertexID) {
    VertexOutput output;
    output.position = float4(float(index == 1) * 4.0f - 1.0f, float(index == 2) * 4.0f - 1.0f, 0.0f, 1.0f);
    output.uv = float2(0.0f);
    return output;
}

[shader("fragment")]
float4 grid_fragment(VertexOutput input) : SV_Target0 {
    float2 pixel = input.position.xy;
    float2 normalized = (pixel / push_data.viewport - 0.5f) * 2.0f;
    float vignette = smoothstep(0.2f, 1.25f, length(normalized));
    float3 background = lerp(float3(0.009f, 0.011f, 0.016f), float3(0.006f, 0.008f, 0.011f), vignette);
    if (push_data.spacing <= 2.0f) return float4(background, 1.0f);
    float2 from_dot = (frac((pixel - push_data.phase) / push_data.spacing + 0.5f) - 0.5f) * push_data.spacing;
    float coverage = (1.0f - smoothstep(0.35f, 1.55f, length(from_dot))) * 0.45f * push_data.grid_opacity;
    float3 color = lerp(background, float3(0.07f, 0.086f, 0.112f), saturate(coverage));
    return float4(color, 1.0f);
}

[shader("vertex")]
VertexOutput picture_vertex(uint index : SV_VertexID) {
    return rectangle_vertex(index);
}

[shader("fragment")]
float4 picture_fragment(VertexOutput input) : SV_Target0 {
    return picture_texture.Sample(picture_sampler, input.uv);
}

[shader("vertex")]
VertexOutput shadow_vertex(uint index : SV_VertexID) {
    return rectangle_vertex(index);
}

[shader("fragment")]
float4 shadow_fragment(VertexOutput input) : SV_Target0 {
    float2 pixel = push_data.rect.xy + input.uv * push_data.rect.zw;
    float2 minimum = push_data.rect.xy + 12.0f;
    float2 maximum = push_data.rect.xy + push_data.rect.zw - 12.0f;
    float2 distance = max(max(minimum - pixel, pixel - maximum), 0.0f);
    return float4(0.0f, 0.0f, 0.0f, 0.24f * exp(-dot(distance, distance) / 40.0f));
}

[shader("vertex")]
VertexOutput shape_vertex(uint index : SV_VertexID) {
    return rectangle_vertex(index);
}

[shader("fragment")]
float4 shape_fragment(VertexOutput input) : SV_Target0 {
    return push_data.color;
}

[shader("vertex")]
VertexOutput group_vertex(uint index : SV_VertexID) {
    return rectangle_vertex(index);
}

[shader("fragment")]
float4 group_fragment(VertexOutput input) : SV_Target0 {
    float2 pixel = input.uv * push_data.rect.zw;
    float2 half_size = push_data.rect.zw * 0.5f;
    float radius = min(push_data.group_style.x, min(half_size.x, half_size.y));
    float2 distance = abs(pixel - half_size) - (half_size - radius);
    float signed_distance = length(max(distance, 0.0f)) + min(max(distance.x, distance.y), 0.0f) - radius;
    float outer = 1.0f - smoothstep(-1.0f, 1.0f, signed_distance);
    float coverage = push_data.group_style.y == 0.0f ? outer :
        outer - (1.0f - smoothstep(-1.0f, 1.0f, signed_distance + push_data.group_style.y));
    return float4(push_data.color.rgb, push_data.color.a * coverage);
}
