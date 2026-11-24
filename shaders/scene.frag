#version 450

layout(location = 0) in vec2 fragment_uv;
layout(location = 1) in vec2 fragment_local;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D textures[64];

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

float rounded_box_distance(vec2 point, vec2 half_size, float radius) {
    const vec2 offset = abs(point) - half_size + radius;
    return length(max(offset, 0.0)) + min(max(offset.x, offset.y), 0.0) - radius;
}

vec4 sample_texture(vec2 uv) {
    if (draw.texture_index < 0) {
        return draw.color;
    }
    if (draw.blur_radius <= 0.0) {
        return texture(textures[draw.texture_index], uv);
    }

    const vec2 texel = vec2(1.0) / vec2(textureSize(textures[draw.texture_index], 0));
    const vec2 step_size = texel * draw.blur_radius;
    vec4 result = texture(textures[draw.texture_index], uv) * 0.2270270270;
    result += texture(textures[draw.texture_index], uv + step_size * vec2(1.0, 0.0)) * 0.1945945946;
    result += texture(textures[draw.texture_index], uv - step_size * vec2(1.0, 0.0)) * 0.1945945946;
    result += texture(textures[draw.texture_index], uv + step_size * vec2(0.0, 1.0)) * 0.1216216216;
    result += texture(textures[draw.texture_index], uv - step_size * vec2(0.0, 1.0)) * 0.1216216216;
    result += texture(textures[draw.texture_index], uv + step_size) * 0.0540540541;
    result += texture(textures[draw.texture_index], uv - step_size) * 0.0540540541;
    result += texture(textures[draw.texture_index], uv + step_size * vec2(1.0, -1.0)) * 0.0162162162;
    result += texture(textures[draw.texture_index], uv + step_size * vec2(-1.0, 1.0)) * 0.0162162162;
    return result;
}

void main() {
    const vec2 half_size = draw.rect.zw * 0.5;
    if (draw.is_shadow != 0) {
        const float base_radius = max(draw.corner_radius - draw.shadow_radius, 0.0);
        const float distance = rounded_box_distance(fragment_local, half_size - draw.shadow_radius,
                                                    base_radius);
        const float alpha = 1.0 - smoothstep(-draw.shadow_radius, 0.0, distance);
        out_color = vec4(draw.color.rgb, draw.color.a * alpha * draw.opacity);
        return;
    }

    vec4 base = sample_texture(fragment_uv);
    const float distance = rounded_box_distance(fragment_local, half_size, draw.corner_radius);
    const float coverage = 1.0 - smoothstep(-1.0, 1.0, distance);
    out_color = vec4(base.rgb, base.a * coverage * draw.opacity);
}
