#version 450

// Push constant for MVP matrices
layout(push_constant) uniform SkyboxPushConstant {
    mat4 view;
    mat4 proj;
} pushConstants;

// Output to fragment shader
layout(location = 0) out vec3 fragTexCoord;

// Cube vertices for skybox
vec3 positions[36] = vec3[](
    // Front face (+Z)
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0, -1.0,  1.0),
    
    // Back face (-Z)
    vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    
    // Left face (-X)
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),
    
    // Right face (+X)
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    
    // Top face (+Y)
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    
    // Bottom face (-Y)
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0)
);

void main() {
    vec3 position = positions[gl_VertexIndex];
    
    // Use the position directly as texture coordinates
    // This ensures seamless cubemap sampling
    fragTexCoord = position;
    
    // Transform position
    mat4 viewNoTranslation = mat4(mat3(pushConstants.view)); // Remove translation
    vec4 clipPos = pushConstants.proj * viewNoTranslation * vec4(position, 1.0);
    
    // Set depth to 1.0 (far plane) for depth test optimization
    gl_Position = clipPos.xyww;
}