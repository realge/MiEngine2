#version 450
#extension GL_ARB_separate_shader_objects : enable

//------------------------------------------------------------------------------
// Skeletal Animation Vertex Shader
// Extends pbr.vert with GPU skinning support
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Uniform buffers
//------------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;           // offset 0   (64 bytes)
    mat4 view;            // offset 64  (64 bytes)
    mat4 proj;            // offset 128 (64 bytes)
    vec4 cameraPos;       // offset 192 (16 bytes) - vec4 to match C++ layout
    float time;           // offset 208 (4 bytes)
    float maxReflectionLod; // offset 212 (4 bytes)
    vec2 _padding;        // offset 216 (8 bytes - pad to 224 for mat4 alignment)
    mat4 lightSpaceMatrix; // offset 224 (64 bytes)
} ubo;

// Bone matrices UBO - set 4 for skeletal meshes (after MVP, Material, Lights, IBL)
layout(set = 4, binding = 0) uniform BoneMatrices {
    mat4 bones[256];  // Max 256 bones
} boneData;

const int MAX_LIGHTS = 16;

// Push constant for model matrix - per-instance data
layout(push_constant) uniform PushConstants {
   layout(offset = 0) mat4 model;           // Model matrix from push constant
   layout(offset = 64) vec4 baseColorFactor;
   layout(offset = 80) float metallicFactor;
   layout(offset = 84) float roughnessFactor;
   layout(offset = 88) float ambientOcclusion;
   layout(offset = 92) float emissiveFactor;
   layout(offset = 96) int hasAlbedoMap;
   layout(offset = 100) int hasNormalMap;
   layout(offset = 104) int hasMetallicRoughnessMap;
   layout(offset = 108) int hasEmissiveMap;
} pushConstants;

//------------------------------------------------------------------------------
// Input Vertex Attributes (SkeletalVertex layout)
//------------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;    // Vertex position
layout(location = 1) in vec3 inColor;       // Vertex color
layout(location = 2) in vec3 inNormal;      // Vertex normal
layout(location = 3) in vec2 inTexCoord;    // Texture coordinates
layout(location = 4) in vec4 inTangent;     // Tangent vector (xyz) + handedness (w)
layout(location = 5) in ivec4 inBoneIndices; // Bone indices (4 influences)
layout(location = 6) in vec4 inBoneWeights;  // Bone weights (must sum to 1.0)

//------------------------------------------------------------------------------
// Output to Fragment Shader
//------------------------------------------------------------------------------
layout(location = 0) out vec3 fragColor;        // Vertex color
layout(location = 1) out vec2 fragTexCoord;     // Texture coordinates
layout(location = 2) out vec3 fragNormal;       // World-space normal
layout(location = 3) out vec3 fragPosition;     // World-space position
layout(location = 4) out mat3 TBN;              // Tangent-Bitangent-Normal matrix
layout(location = 7) out vec3 fragViewDir;      // View direction in world space

//------------------------------------------------------------------------------
// Shadow Stuff
//------------------------------------------------------------------------------
layout(location = 8) out vec4 fragPosLightSpace;

//------------------------------------------------------------------------------
// Skinning Functions
//------------------------------------------------------------------------------

// Compute the skinned transform matrix from bone influences
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

    // Fallback to identity if no weights (shouldn't happen with normalized weights)
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    if (totalWeight < 0.001) {
        return mat4(1.0);
    }

    return skinMatrix;
}

void main() {
    //--------------------------------------------------------------------------
    // Skinning - Transform vertex by bone matrices
    //--------------------------------------------------------------------------
    mat4 skinMatrix = computeSkinMatrix();

    // Apply skinning to position
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);

    // Apply skinning to normal (use 3x3 part, no translation)
    mat3 skinMatrix3x3 = mat3(skinMatrix);
    vec3 skinnedNormal = normalize(skinMatrix3x3 * inNormal);

    // Apply skinning to tangent
    vec3 skinnedTangent = normalize(skinMatrix3x3 * inTangent.xyz);

    //--------------------------------------------------------------------------
    // Position Transformation
    //--------------------------------------------------------------------------
    // Transform skinned vertex to world space using the instance's model matrix
    vec4 worldPosition = pushConstants.model * skinnedPosition;

    // Save world position for fragment shader
    fragPosition = worldPosition.xyz;

    // Calculate position in light space
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPosition;

    // Output clip space position
    gl_Position = ubo.proj * ubo.view * worldPosition;

    //--------------------------------------------------------------------------
    // Normal and Tangent Space Calculation
    //--------------------------------------------------------------------------
    mat3 modelMatrix3x3 = mat3(pushConstants.model);

    // Transform skinned normal to world space and normalize
    vec3 N = normalize(modelMatrix3x3 * skinnedNormal);

    // Transform skinned tangent to world space
    vec3 T = normalize(modelMatrix3x3 * skinnedTangent);

    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);

    // Calculate bitangent with correct handedness
    vec3 B = cross(N, T) * inTangent.w;

    // Create TBN matrix for transforming from tangent to world space
    TBN = mat3(T, B, N);

    // Output the world space normal directly
    fragNormal = N;

    //--------------------------------------------------------------------------
    // View Direction Calculation
    //--------------------------------------------------------------------------
    fragViewDir = ubo.cameraPos.xyz - fragPosition;

    //--------------------------------------------------------------------------
    // Pass-through Attributes
    //--------------------------------------------------------------------------
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
