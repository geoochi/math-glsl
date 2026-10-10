#version 410 core

// Fullscreen triangle generated from gl_VertexID -- no vertex buffer needed.
// 3 vertices cover the whole clip-space square with a bit of overscan, which is
// cheaper than two triangles and avoids the diagonal seam.
void main() {
    vec2 v[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(v[gl_VertexID], 0.0, 1.0);
}
