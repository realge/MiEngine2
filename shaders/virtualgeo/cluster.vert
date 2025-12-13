#version 450
#extension GL_ARB_separate_shader_objects : enable

// ============================================================================
// Virtual Geometry Cluster Vertex Shader
// Supports both direct drawing (push constant) and GPU-driven (instance buffer)
// ============================================================================

// Vertex input (ClusterVertex - 48 bytes)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

// Uniform buffer - camera and lighting (shared across all instances)
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
} ubo;

// Instance data for GPU-driven mode (matches GPUInstanceData in VirtualGeoRenderer.h)
struct InstanceData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    uint clusterOffset;
    uint clusterCount;
    uint pad0;
    uint pad1;
};

// Instance buffer - for GPU-driven mode (binding 1)
layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;             // 64 bytes - used in direct mode
    uint debugMode;         // 4 bytes
    uint lodLevel;          // 4 bytes
    uint clusterId;         // 4 bytes
    uint useInstanceBuffer; // 4 bytes - 1 = GPU-driven, 0 = direct
} push;                     // Total: 80 bytes

void main() {
    mat4 modelMatrix;
    mat3 normalMatrix;

    if (push.useInstanceBuffer != 0) {
        // GPU-driven mode: gl_InstanceIndex = firstInstance from indirect draw
        modelMatrix = instances[gl_InstanceIndex].modelMatrix;
        normalMatrix = mat3(instances[gl_InstanceIndex].normalMatrix);
    } else {
        // Direct mode: use push constant
        modelMatrix = push.model;
        normalMatrix = transpose(inverse(mat3(modelMatrix)));
    }

    // Transform to world space
    vec4 worldPos = modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;

    gl_Position = ubo.projection * ubo.view * worldPos;
}
