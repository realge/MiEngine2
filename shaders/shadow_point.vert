#version 450

layout(location = 0) in vec3 inPosition;

// Output world position for fragment shader to calculate distance
layout(location = 0) out vec3 fragWorldPos;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) int faceIndex;
} pushConstants;

layout(set = 0, binding = 0) uniform ShadowUniformBuffer {
    mat4 lightViewProj[6];  // 6 face matrices
    vec4 lightPos;          // xyz = position, w = far plane
} ubo;

void main() {
    vec4 worldPos = pushConstants.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = ubo.lightViewProj[pushConstants.faceIndex] * worldPos;
}
