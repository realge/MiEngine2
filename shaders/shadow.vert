#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
} pushConstants;

// We will pass the Light View-Projection matrix via a specific set or Push Constant.
// To keep it compatible with your existing setup, let's use a new Descriptor Set or re-use a UBO.
// For simplicity here, I'll assume we pass the LightSpaceMatrix via a separate descriptor set or push constant.
// HOWEVER, typically the LightSpaceMatrix is constant for the whole pass.
layout(set = 0, binding = 0) uniform ShadowUniformBuffer {
    mat4 lightSpaceMatrix;
} ubo;

void main() {
    gl_Position = ubo.lightSpaceMatrix * pushConstants.model * vec4(inPosition, 1.0);
}