#version 450
#extension GL_ARB_separate_shader_objects : enable

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
// Input Vertex Attributes
//------------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;    // Vertex position
layout(location = 1) in vec3 inColor;       // Vertex color
layout(location = 2) in vec3 inNormal;      // Vertex normal
layout(location = 3) in vec2 inTexCoord;    // Texture coordinates
layout(location = 4) in vec4 inTangent;     // Tangent vector (xyz) + handedness (w)

//------------------------------------------------------------------------------
// Output to Fragment Shader
//------------------------------------------------------------------------------
layout(location = 0) out vec3 fragColor;        // Vertex color
layout(location = 1) out vec2 fragTexCoord;     // Texture coordinates
layout(location = 2) out vec3 fragNormal;       // World-space normal
layout(location = 3) out vec3 fragPosition;     // World-space position
layout(location = 4) out mat3 TBN;              // Tangent-Bitangent-Normal matrix
layout(location = 7) out vec3 fragViewDir;      // View direction in world space (not tangent space)

//------------------------------------------------------------------------------
// Shadow Stuff
//------------------------------------------------------------------------------
layout(location = 8) out vec4 fragPosLightSpace;

void main() {
    //--------------------------------------------------------------------------
    // Position Transformation
    //--------------------------------------------------------------------------
    // Transform vertex to world space using the instance's model matrix
    vec4 worldPosition = pushConstants.model * vec4(inPosition, 1.0);
    
    // Save world position for fragment shader
    fragPosition = worldPosition.xyz;
	
	// Calculate position in light space
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPosition; 
    
    // Output clip space position
    gl_Position = ubo.proj * ubo.view * worldPosition;
    
    //--------------------------------------------------------------------------
    // Normal and Tangent Space Calculation
    //--------------------------------------------------------------------------
    // For uniform scaling, we can use the model matrix directly
    // For non-uniform scaling, we'd need the inverse transpose, but that's expensive
    // Most engines pre-calculate this on the CPU
    mat3 modelMatrix3x3 = mat3(pushConstants.model);
    
    // Transform normal to world space and normalize
    vec3 N = normalize(modelMatrix3x3 * inNormal);
    
    // Transform tangent to world space
    vec3 T = normalize(modelMatrix3x3 * inTangent.xyz);
    
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    // This is important to handle non-orthogonal TBN from artists/exporters
    T = normalize(T - dot(T, N) * N);
    
    // Calculate bitangent with correct handedness
    vec3 B = cross(N, T) * inTangent.w;
    
    // Create TBN matrix for transforming from tangent to world space
    // Note: This is the transpose of what you might expect
    // because we want to transform FROM tangent TO world space
    TBN = mat3(T, B, N);
    
    // Output the world space normal directly
    fragNormal = N;
    
    //--------------------------------------------------------------------------
    // View Direction Calculation
    //--------------------------------------------------------------------------
    // Calculate view direction in world space (fragment shader will normalize)
    // We don't transform to tangent space here - let the fragment shader decide
    fragViewDir = ubo.cameraPos.xyz - fragPosition;
    
    //--------------------------------------------------------------------------
    // Pass-through Attributes
    //--------------------------------------------------------------------------
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}