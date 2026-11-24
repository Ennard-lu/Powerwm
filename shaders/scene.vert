#version 450

layout(location = 0) out vec2 fragment_uv;
layout(location = 1) out vec2 fragment_local;

layout(push_constant) uniform draw_constants {
    vec2 viewport;
    vec4 rect;
    vec4 color;
    float rotation;
    float scale;
    float corner_radius;
    float shadow_radius;
    float blur_radius;
    float opacity;
    int texture_index;
    int is_shadow;
} draw;

void main() {
    const vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    const vec2 unit = uv * 2.0 - 1.0;
    const vec2 half_size = draw.rect.zw * 0.5;
    const vec2 local = unit * half_size;

    const float cosine = cos(draw.rotation);
    const float sine = sin(draw.rotation);
    const vec2 rotated = vec2(local.x * cosine - local.y * sine,
                              local.x * sine + local.y * cosine) * draw.scale;
    const vec2 center = draw.rect.xy + half_size;
    const vec2 pixel = center + rotated;

    gl_Position = vec4(pixel.x / draw.viewport.x * 2.0 - 1.0,
                       pixel.y / draw.viewport.y * 2.0 - 1.0, 0.0, 1.0);
    fragment_uv = uv;
    fragment_local = local;
}
