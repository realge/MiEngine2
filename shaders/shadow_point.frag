#version 450

layout(location = 0) in vec3 fragWorldPos;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) int faceIndex;
} pushConstants;

layout(set = 0, binding = 0) uniform ShadowUniformBuffer {
    mat4 lightViewProj[6];
    vec4 lightPos;  // xyz = position, w = far plane
} ubo;

void main() {
    // Calculate distance from light to fragment
    float lightDistance = length(fragWorldPos - ubo.lightPos.xyz);

    // Map to [0, 1] range using far plane
    // This is written to the depth buffer for later comparison
    gl_FragDepth = lightDistance / ubo.lightPos.w;
}
