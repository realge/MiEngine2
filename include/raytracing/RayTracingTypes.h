#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>

namespace MiEngine {

// ============================================================================
// Ray Tracing Settings
// ============================================================================

struct RTSettings {
    bool enabled = false;
    int samplesPerPixel = 1;        // 1-4 for real-time
    int maxBounces = 2;             // Reflection bounces
    float reflectionBias = 0.05f;   // Higher default to prevent self-reflection artifacts
    float shadowBias = 0.05f;       // Higher default to prevent self-shadowing on curved surfaces
    bool enableReflections = true;
    bool enableSoftShadows = true;
    float shadowSoftness = 0.02f;   // Light source radius for soft shadows
    bool enableDenoising = true;
    int debugMode = 0;              // 0=off, 1=normals, 2=depth, 3=reflections only, 4=shadows only
};

// ============================================================================
// Acceleration Structure Types
// ============================================================================

struct BLASInfo {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    uint32_t meshId = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool isBuilt = false;

    // RT-specific geometry buffers (with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceAddress vertexBufferAddress = 0;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceAddress indexBufferAddress = 0;

    // Offsets into global geometry buffer (for bindless access)
    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;
};

struct TLASInfo {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    void* instanceMapped = nullptr;
    uint32_t instanceCount = 0;
    bool isBuilt = false;
};

// ============================================================================
// RT Geometry Data (for bindless access in shaders)
// ============================================================================

// Packed vertex for RT (position + normal + UV)
struct RTVertex {
    glm::vec3 position;
    float pad0;
    glm::vec3 normal;
    float pad1;
    glm::vec2 texCoord;
    glm::vec2 pad2;
};

// Per-instance data accessible in shaders
struct RTInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;     // Inverse transpose of model matrix
    uint32_t materialIndex;
    uint32_t vertexBufferOffset;
    uint32_t indexBufferOffset;
    uint32_t flags;             // Bit 0: cast shadows, Bit 1: receive shadows, Bit 2: visible in reflections
};

// Per-mesh geometry info for looking up vertex data
struct RTMeshInfo {
    uint32_t vertexOffset;      // Offset into global vertex buffer
    uint32_t indexOffset;       // Offset into global index buffer
    uint32_t vertexCount;
    uint32_t indexCount;
};

// Material data for RT shaders (matches PBR material)
struct RTMaterialData {
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float emissive;
    int32_t albedoTexIndex;
    int32_t normalTexIndex;
    int32_t metallicRoughnessTexIndex;
    int32_t emissiveTexIndex;
    uint32_t meshId;        // Index into mesh info buffer for vertex lookup
    uint32_t padding[3];    // Pad to 64 bytes for alignment
};

// ============================================================================
// RT Uniform Buffer (passed to shaders)
// ============================================================================

struct RTUniformBuffer {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    glm::vec4 cameraPosition;       // xyz = position, w = unused
    glm::vec4 lightDirection;       // xyz = direction (normalized), w = intensity
    glm::vec4 lightColor;           // rgb = color, a = unused
    int32_t frameNumber;
    int32_t samplesPerPixel;
    int32_t maxBounces;
    float reflectionBias;
    float shadowBias;
    float shadowSoftness;
    int32_t flags;                  // Bit 0: enable reflections, Bit 1: enable shadows, Bit 2: use IBL
    int32_t debugMode;
};

// ============================================================================
// Shader Binding Table Info
// ============================================================================

struct SBTRegion {
    VkStridedDeviceAddressRegionKHR raygen{};
    VkStridedDeviceAddressRegionKHR miss{};
    VkStridedDeviceAddressRegionKHR hit{};
    VkStridedDeviceAddressRegionKHR callable{};
};

// ============================================================================
// RT Pipeline Properties (queried from device)
// ============================================================================

struct RTPipelineProperties {
    uint32_t shaderGroupHandleSize = 0;
    uint32_t shaderGroupHandleAlignment = 0;
    uint32_t shaderGroupBaseAlignment = 0;
    uint32_t maxRayRecursionDepth = 0;
    uint32_t maxShaderGroupStride = 0;
};

struct RTAccelerationStructureProperties {
    uint64_t maxGeometryCount = 0;
    uint64_t maxInstanceCount = 0;
    uint64_t maxPrimitiveCount = 0;
    uint32_t minAccelerationStructureScratchOffsetAlignment = 0;
};

// ============================================================================
// RT Feature Support
// ============================================================================

struct RTFeatureSupport {
    bool supported = false;
    bool accelerationStructure = false;
    bool rayTracingPipeline = false;
    bool rayQuery = false;
    bool bufferDeviceAddress = false;
    std::string unsupportedReason;
};

// ============================================================================
// Denoiser Settings
// ============================================================================

struct DenoiserSettings {
    bool enableTemporal = true;
    bool enableSpatial = false;     // Disabled by default - temporal alone is usually enough
    float temporalBlend = 0.1f;     // 10% current, 90% history - accumulates more for noise reduction
    float varianceClipGamma = 1.5f; // Tighter clipping to reduce ghosting (lower = less ghosting, more noise)
    int spatialFilterRadius = 1;    // 1 = 3x3 kernel (fast), 2 = 5x5, 3 = 7x7 (slow)
    float spatialColorSigma = 0.5f; // Color similarity weight (higher = more blur)
    float spatialSigma = 1.5f;      // Spatial falloff (larger = wider blur)
};

// ============================================================================
// Denoiser Uniform Buffers
// ============================================================================

struct TemporalDenoiseUniforms {
    glm::mat4 prevViewProj;
    glm::mat4 currViewProjInv;
    glm::vec4 cameraPos;
    float temporalBlend;
    float varianceClipGamma;
    int32_t frameNumber;
    int32_t enableTemporal;
};

struct SpatialDenoiseUniforms {
    float sigmaColor;
    float sigmaSpatial;
    int32_t kernelRadius;
    int32_t enabled;
};

// ============================================================================
// Constants
// ============================================================================

constexpr uint32_t RT_MAX_INSTANCES = 4096;
constexpr uint32_t RT_MAX_MATERIALS = 256;
constexpr uint32_t RT_MAX_TEXTURES = 512;
constexpr uint32_t RT_SHADER_GROUP_RAYGEN = 0;
constexpr uint32_t RT_SHADER_GROUP_MISS = 1;
constexpr uint32_t RT_SHADER_GROUP_MISS_SHADOW = 2;
constexpr uint32_t RT_SHADER_GROUP_HIT = 3;
constexpr uint32_t RT_SHADER_GROUP_COUNT = 4;

// Instance flags
constexpr uint32_t RT_INSTANCE_FLAG_CAST_SHADOW = 1 << 0;
constexpr uint32_t RT_INSTANCE_FLAG_RECEIVE_SHADOW = 1 << 1;
constexpr uint32_t RT_INSTANCE_FLAG_VISIBLE_IN_REFLECTION = 1 << 2;
constexpr uint32_t RT_INSTANCE_FLAG_DEFAULT = RT_INSTANCE_FLAG_CAST_SHADOW |
                                               RT_INSTANCE_FLAG_RECEIVE_SHADOW |
                                               RT_INSTANCE_FLAG_VISIBLE_IN_REFLECTION;

} // namespace MiEngine
