#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;      // Must match SkeletalVertex layout
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in ivec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
} pushConstants;

layout(set = 0, binding = 0) uniform ShadowUniformBuffer {
    mat4 lightSpaceMatrix;
} ubo;

layout(set = 1, binding = 0) uniform BoneMatrices {
    mat4 bones[256];
} boneData;

// Compute the skinned transform matrix from bone influences (must match skeletal.vert)
mat4 computeSkinMatrix() {
    mat4 skinMatrix = mat4(0.0);

    // Accumulate weighted bone transforms
    if (inBoneWeights.x > 0.0) {
        skinMatrix += inBoneWeights.x * boneData.bones[inBoneIndices.x];
    }
    if (inBoneWeights.y > 0.0) {
        skinMatrix += inBoneWeights.y * boneData.bones[inBoneIndices.y];
    }
    if (inBoneWeights.z > 0.0) {
        skinMatrix += inBoneWeights.z * boneData.bones[inBoneIndices.z];
    }
    if (inBoneWeights.w > 0.0) {
        skinMatrix += inBoneWeights.w * boneData.bones[inBoneIndices.w];
    }

    // Fallback to identity if no weights
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    if (totalWeight < 0.001) {
        return mat4(1.0);
    }

    return skinMatrix;
}

void main() {
    // Calculate skinned position (must match skeletal.vert exactly)
    mat4 skinMatrix = computeSkinMatrix();
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);

    // Transform to world space
    vec4 worldPosition = pushConstants.model * skinnedPosition;

    gl_Position = ubo.lightSpaceMatrix * worldPosition;
}
