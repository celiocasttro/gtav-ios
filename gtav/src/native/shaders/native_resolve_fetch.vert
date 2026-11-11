// gtav - native renderer: in-pass colour resolve (see native_resolve_fetch.frag). A triangle over
// the viewport, which is the resolve rectangle.
#version 450
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
