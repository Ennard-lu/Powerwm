#version 450

layout(location = 0) out vec3 fragment_color;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    const vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    fragment_color = vec3((position.x + 1.0) * 0.5, (position.y + 1.0) * 0.5, 0.5);
}
