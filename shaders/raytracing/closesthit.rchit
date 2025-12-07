#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : enable

#include "rt_common.glsl"

// ============================================================================
// Descriptor Bindings (All in Set 0)
// ============================================================================

// Binding 0: Acceleration structure
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

// Binding 3: Camera and settings
layout(set = 0, binding = 3) uniform RTUniforms {
    mat4 viewInverse;
    mat4 projInverse;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    int frameNumber;
    int samplesPerPixel;
    int maxBounces;
    float reflectionBias;
    float shadowBias;
    float shadowSoftness;
    int flags;
    int debugMode;
} uniforms;

// Binding 4: Environment map
layout(set = 0, binding = 4) uniform samplerCube environmentMap;

// Binding 5: Material buffer (per-instance materials)
struct RTMaterialData {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float emissive;
    int albedoTexIndex;
    int normalTexIndex;
    int metallicRoughnessTexIndex;
    int emissiveTexIndex;
    uint meshId;        // Index into mesh info buffer
    uint padding0;
    uint padding1;
    uint padding2;
};

layout(set = 0, binding = 5) readonly buffer MaterialBuffer {
    RTMaterialData materials[];
};

// Binding 6: Global vertex buffer
// Vertex layout matches CommonVertex.h (60 bytes per vertex):
// vec3 position (12), vec3 color (12), vec3 normal (12), vec2 texCoord (8), vec4 tangent (16)
struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;
    vec4 tangent;
};

layout(set = 0, binding = 6) readonly buffer VertexBuffer {
    // Read as floats since Vertex struct has padding issues
    float vertexData[];
};

// Binding 7: Global index buffer
layout(set = 0, binding = 7) readonly buffer IndexBuffer {
    uint indices[];
};

// Binding 8: Mesh info buffer
struct MeshInfo {
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};

layout(set = 0, binding = 8) readonly buffer MeshInfoBuffer {
    MeshInfo meshInfos[];
};

// ============================================================================
// Hit Attributes
// ============================================================================

hitAttributeEXT vec2 hitAttribs;  // Barycentric coordinates

// ============================================================================
// Ray Payload
// ============================================================================

layout(location = 0) rayPayloadInEXT struct RayPayload {
    vec3 color;
    float hitT;
    vec3 normal;
    float metallic;
    vec3 albedo;    // Base color for tinting reflections
    float roughness;
    int hit;
} payload;

// ============================================================================
// Helper: Read vertex from buffer
// ============================================================================

// Vertex stride in floats: 3 (pos) + 3 (color) + 3 (normal) + 2 (uv) + 4 (tangent) = 15 floats
const uint VERTEX_STRIDE = 15;

vec3 getVertexPosition(uint vertexIndex) {
    uint base = vertexIndex * VERTEX_STRIDE;
    return vec3(vertexData[base], vertexData[base + 1], vertexData[base + 2]);
}

vec3 getVertexNormal(uint vertexIndex) {
    uint base = vertexIndex * VERTEX_STRIDE + 6;  // Skip position (3) and color (3)
    return vec3(vertexData[base], vertexData[base + 1], vertexData[base + 2]);
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Get barycentric coordinates
    const vec3 barycentrics = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);

    // Get hit information
    payload.hitT = gl_HitTEXT;
    payload.hit = 1;

    // Get material from buffer using instance custom index
    uint instanceIndex = gl_InstanceCustomIndexEXT;
    RTMaterialData mat = materials[instanceIndex];

    // Get mesh info for vertex lookup
    uint meshId = mat.meshId;
    MeshInfo meshInfo = meshInfos[meshId];

    // Get triangle indices
    // gl_PrimitiveID gives us the triangle index within this BLAS
    uint triIndex = gl_PrimitiveID;
    uint i0 = indices[meshInfo.indexOffset + triIndex * 3 + 0];
    uint i1 = indices[meshInfo.indexOffset + triIndex * 3 + 1];
    uint i2 = indices[meshInfo.indexOffset + triIndex * 3 + 2];

    // Add vertex offset to get global vertex indices
    i0 += meshInfo.vertexOffset;
    i1 += meshInfo.vertexOffset;
    i2 += meshInfo.vertexOffset;

    // Get vertex normals
    vec3 n0 = getVertexNormal(i0);
    vec3 n1 = getVertexNormal(i1);
    vec3 n2 = getVertexNormal(i2);

    // Interpolate normal using barycentric coordinates
    vec3 objectNormal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);

    // Transform normal to world space
    mat3 normalMatrix = mat3(gl_ObjectToWorldEXT);
    vec3 worldNormal = normalize(normalMatrix * objectNormal);

    // Ensure normal faces toward the incoming ray
    vec3 incomingDir = gl_WorldRayDirectionEXT;
    if (dot(worldNormal, incomingDir) > 0.0) {
        worldNormal = -worldNormal;
    }

    payload.normal = worldNormal;

    // Calculate world-space hit position
    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // View direction is the INCOMING ray direction (negated)
    // This is correct for both primary rays AND reflection rays
    vec3 viewDir = -gl_WorldRayDirectionEXT;
    vec3 lightDir = normalize(-uniforms.lightDirection.xyz);
    vec3 halfVec = normalize(viewDir + lightDir);

    // Basic Lambert + Blinn-Phong
    float NdotL = max(dot(payload.normal, lightDir), 0.0);
    float NdotH = max(dot(payload.normal, halfVec), 0.0);

    vec3 albedo = mat.baseColor.rgb;
    float roughness = mat.roughness;
    float metallic = mat.metallic;

    // Pass material properties back to raygen via payload
    payload.metallic = metallic;
    payload.roughness = roughness;
    payload.albedo = albedo;

    // Fresnel
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(max(dot(halfVec, viewDir), 0.0), F0);

    // Get light intensity from w component
    float lightIntensity = uniforms.lightDirection.w;
    vec3 lightColor = uniforms.lightColor.rgb;

    // Diffuse lighting (Lambert)
    vec3 diffuse = albedo * (1.0 - metallic) * NdotL * lightColor * lightIntensity;

    // Specular lighting (Blinn-Phong approximation of GGX)
    float specPower = max(1.0, (1.0 - roughness) * 128.0);
    vec3 specular = F * pow(NdotH, specPower) * NdotL * lightColor * lightIntensity;

    // Ambient lighting using hemisphere approximation
    // This gives consistent results for both IBL and non-IBL modes
    // and avoids the "transparency" effect from HDR environment sampling
    vec3 skyColor = vec3(0.5, 0.6, 0.8);      // Blue-ish sky
    vec3 groundColor = vec3(0.25, 0.2, 0.15); // Brown-ish ground
    float hemisphereBlend = payload.normal.y * 0.5 + 0.5;
    vec3 hemisphereAmbient = mix(groundColor, skyColor, hemisphereBlend);
    vec3 ambient = hemisphereAmbient * 0.4 * albedo * mat.ao;

    // Combine lighting
    payload.color = ambient + diffuse + specular;
}
