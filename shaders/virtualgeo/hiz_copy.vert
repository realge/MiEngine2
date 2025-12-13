#version 450

// Fullscreen triangle vertex shader for Hi-Z depth copy
// Generates a fullscreen triangle without any vertex input

layout(location = 0) out vec2 outUV;

void main() {
    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);

    // UV coordinates: (0,0) to (1,1)
    // Vulkan has Y pointing down, so flip V
    outUV = positions[gl_VertexIndex] * 0.5 + 0.5;
    outUV.y = 1.0 - outUV.y;
}
