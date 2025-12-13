#pragma once

#include "VirtualGeoTypes.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>

// Forward declarations
class VulkanRenderer;

namespace MiEngine {

// ============================================================================
// GPU Buffer Structures (match shader layouts)
// Note: GPUClusterData is already defined in VirtualGeoTypes.h
// ============================================================================

// Indirect draw command (VkDrawIndexedIndirectCommand)
struct GPUDrawCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// Per-instance transform data for rendering
struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;      // transpose(inverse(modelMatrix))
    uint32_t clusterOffset;      // Start index in cluster buffer
    uint32_t clusterCount;       // Number of clusters for this instance
    uint32_t pad0, pad1;
};

// Culling uniforms
struct GPUCullingUniforms {
    glm::mat4 viewProjection;
    glm::mat4 view;
    glm::vec4 frustumPlanes[6];  // xyz = normal, w = distance
    glm::vec4 cameraPosition;    // xyz = pos, w = unused
    glm::vec4 screenParams;      // x = width, y = height, z = near, w = far
    float lodBias;               // LOD selection bias
    float errorThreshold;        // Screen-space error threshold in pixels
    uint32_t totalClusters;
    uint32_t frameIndex;
    uint32_t forcedLodLevel;     // If > 0, force all clusters to this LOD
    uint32_t useForcedLod;       // 1 = use forced LOD, 0 = auto LOD selection
    uint32_t enableFrustumCulling;  // 1 = enable frustum culling, 0 = disable
    uint32_t enableOcclusionCulling;// 1 = enable Hi-Z occlusion culling, 0 = disable
    // Hi-Z occlusion parameters (adjustable via debug panel)
    float hizMaxMipLevel;        // Maximum mip level to sample (lower = more accurate)
    float hizDepthBias;          // Bias added to Hi-Z depth for comparison
    float hizDepthThreshold;     // Depth threshold for "no occluder" detection
    float hizPadding;            // Padding for alignment
};

// Render uniforms (matches shader UBO - shared across all instances)
struct VGRenderUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection;  // xyz = direction, w = intensity
    glm::vec4 lightColor;      // rgb = color, a = ambient
};

// Push constants for per-instance data (matches shader push_constant)
struct VGPushConstants {
    glm::mat4 model;           // 64 bytes - used in direct mode
    uint32_t debugMode;        // 4 bytes
    uint32_t lodLevel;         // 4 bytes
    uint32_t clusterId;        // 4 bytes
    uint32_t useInstanceBuffer;// 4 bytes - 1 = GPU-driven, 0 = direct
};  // Total: 80 bytes

// ============================================================================
// Clustered Mesh GPU Representation
// ============================================================================

// Per-LOD index range for selective rendering
struct LODIndexRange {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t clusterCount = 0;
};

// Extended GPU cluster data with global index offset for indirect draw
struct GPUClusterDataExt {
    glm::vec4 boundingSphere;        // xyz = center, w = radius
    glm::vec4 aabbMin;               // xyz = min, w = lodError
    glm::vec4 aabbMax;               // xyz = max, w = parentError
    uint32_t vertexOffset;           // Global vertex offset
    uint32_t vertexCount;
    uint32_t globalIndexOffset;      // Global offset into combined index buffer
    uint32_t triangleCount;
    uint32_t lodLevel;
    uint32_t materialIndex;
    uint32_t flags;
    uint32_t instanceId;             // Which instance this cluster belongs to
};

// Per-frame GPU resources to avoid read-after-write hazards
struct PerFrameResources {
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkBuffer drawCountBuffer = VK_NULL_HANDLE;
    VkBuffer visibleClusterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
    VkDeviceMemory drawCountMemory = VK_NULL_HANDLE;
    VkDeviceMemory visibleClusterMemory = VK_NULL_HANDLE;
    VkDescriptorSet cullingDescSet = VK_NULL_HANDLE;
};

// Global merged buffer for all meshes (GPU-driven mode)
struct MergedMeshData {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkBuffer clusterBuffer = VK_NULL_HANDLE;  // GPUClusterDataExt[]
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceMemory clusterMemory = VK_NULL_HANDLE;
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    uint32_t totalClusters = 0;
    bool dirty = true;  // Needs rebuild when meshes added/removed
};

struct ClusteredMeshGPU {
    uint32_t meshId;

    // GPU Buffers (per-mesh, used in non-GPU-driven mode)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkBuffer clusterBuffer = VK_NULL_HANDLE;  // GPUClusterData[]

    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceMemory clusterMemory = VK_NULL_HANDLE;

    // Counts
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t clusterCount = 0;
    uint32_t maxLodLevel = 0;

    // Per-LOD index ranges for selective LOD rendering
    std::vector<LODIndexRange> lodRanges;

    // Bounding volumes (world space after transform)
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;

    // Source data cache for merged buffer rebuilding
    std::vector<ClusterVertex> sourceVertices;
    std::vector<uint32_t> sourceIndices;  // Already converted to global indices
    std::vector<Cluster> sourceClusters;

    // Global offsets (set when merged buffers are built)
    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;
    uint32_t globalClusterOffset = 0;
};

// ============================================================================
// Virtual Geometry Renderer
// ============================================================================

class VirtualGeoRenderer {
public:
    VirtualGeoRenderer();
    ~VirtualGeoRenderer();

    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void cleanup();

    // Mesh management
    uint32_t uploadClusteredMesh(const ClusteredMesh& mesh);
    void removeClusteredMesh(uint32_t meshId);

    // Instance management
    uint32_t addInstance(uint32_t meshId, const glm::mat4& transform);
    void updateInstance(uint32_t instanceId, const glm::mat4& transform);
    void removeInstance(uint32_t instanceId);

    // Rendering
    void beginFrame(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);

    // Call outside render pass to run compute culling
    void dispatchCulling(VkCommandBuffer cmd);

    // Call inside render pass to draw visible clusters
    void draw(VkCommandBuffer cmd);
    void endFrame();

    // Debug and LOD control
    void setLodBias(float bias) { m_lodBias = bias; }
    float getLodBias() const { return m_lodBias; }
    void setErrorThreshold(float threshold) { m_errorThreshold = threshold; }
    float getErrorThreshold() const { return m_errorThreshold; }
    void setFrustumCullingEnabled(bool enabled) { m_frustumCullingEnabled = enabled; }
    bool isFrustumCullingEnabled() const { return m_frustumCullingEnabled; }
    void setLodSelectionEnabled(bool enabled) { m_lodSelectionEnabled = enabled; }
    bool isLodSelectionEnabled() const { return m_lodSelectionEnabled; }
    void setDebugMode(uint32_t mode) { m_debugMode = mode; }
    uint32_t getDebugMode() const { return m_debugMode; }
    void setForcedLodLevel(uint32_t level) { m_forcedLodLevel = level; }
    uint32_t getForcedLodLevel() const { return m_forcedLodLevel; }
    uint32_t getMaxLodLevel() const;  // Returns max LOD across all meshes

    // GPU-driven rendering mode
    void setGPUDrivenEnabled(bool enabled) { m_gpuDrivenEnabled = enabled; }
    bool isGPUDrivenEnabled() const { return m_gpuDrivenEnabled; }

    // Hi-Z occlusion culling
    void setOcclusionCullingEnabled(bool enabled) { m_occlusionCullingEnabled = enabled; }
    bool isOcclusionCullingEnabled() const { return m_occlusionCullingEnabled; }
    void buildHiZPyramid(VkCommandBuffer cmd, VkImageView depthView);
    uint32_t getHiZMipLevels() const { return m_hizMipLevels; }

    // Hi-Z adjustable parameters
    void setHiZMaxMipLevel(float level) { m_hizMaxMipLevel = level; }
    float getHiZMaxMipLevel() const { return m_hizMaxMipLevel; }
    void setHiZDepthBias(float bias) { m_hizDepthBias = bias; }
    float getHiZDepthBias() const { return m_hizDepthBias; }
    void setHiZDepthThreshold(float threshold) { m_hizDepthThreshold = threshold; }
    float getHiZDepthThreshold() const { return m_hizDepthThreshold; }

    // Hi-Z debug visualization
    void setHiZDebugEnabled(bool enabled) { m_hizDebugEnabled = enabled; }
    bool isHiZDebugEnabled() const { return m_hizDebugEnabled; }
    void setHiZDebugMipLevel(float level) { m_hizDebugMipLevel = level; }
    float getHiZDebugMipLevel() const { return m_hizDebugMipLevel; }
    void setHiZDebugMode(uint32_t mode) { m_hizDebugMode = mode; }  // 0=grayscale, 1=heatmap
    uint32_t getHiZDebugMode() const { return m_hizDebugMode; }
    void drawHiZDebug(VkCommandBuffer cmd);  // Call inside render pass

    // Statistics
    uint32_t getVisibleClusterCount() const { return m_visibleClusterCount; }
    uint32_t getTotalClusterCount() const { return m_totalClusterCount; }
    uint32_t getDrawCallCount() const { return m_drawCallCount; }
    uint32_t getMeshCount() const { return static_cast<uint32_t>(m_meshes.size()); }
    uint32_t getInstanceCount() const { return static_cast<uint32_t>(m_instances.size()); }

    // Check if ready to render
    bool isInitialized() const { return m_initialized; }

private:
    // Pipeline creation
    bool createPipelines();
    bool createCullingPipeline();
    bool createRenderingPipeline();
    bool createDescriptorSets();

    // Buffer management
    bool createIndirectBuffer(uint32_t maxDraws);
    bool createClusterVisibilityBuffer(uint32_t maxClusters);
    bool createInstanceBuffer();
    void updateCullingUniforms();
    void updateDescriptorSets(VkBuffer clusterBuffer, VkDeviceSize clusterBufferSize);
    void uploadInstanceData();

    // Per-frame resources for GPU-driven mode
    bool createPerFrameResources();
    void cleanupPerFrameResources();
    void updatePerFrameDescriptorSet(uint32_t frameIndex);

    // Merged buffer management for GPU-driven mode
    bool rebuildMergedBuffers();
    void cleanupMergedBuffers();

    // Hi-Z resources
    bool createHiZResources(uint32_t width, uint32_t height);
    bool createHiZPipeline();
    bool createHiZDebugPipeline();
    bool createHiZCopyPipeline();  // Graphics pass to copy depth to Hi-Z mip 0
    void cleanupHiZResources();

    // Helper functions
    void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    VulkanRenderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;

    // Meshes and instances
    std::unordered_map<uint32_t, ClusteredMeshGPU> m_meshes;
    std::unordered_map<uint32_t, GPUInstanceData> m_instances;
    uint32_t m_nextMeshId = 1;
    uint32_t m_nextInstanceId = 1;

    // Global buffers (non-GPU-driven mode)
    VkBuffer m_indirectBuffer = VK_NULL_HANDLE;          // GPUDrawCommand[]
    VkBuffer m_visibleClusterBuffer = VK_NULL_HANDLE;    // Visible cluster indices
    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;          // GPUInstanceData[]
    VkBuffer m_cullingUniformBuffer = VK_NULL_HANDLE;    // GPUCullingUniforms
    VkBuffer m_drawCountBuffer = VK_NULL_HANDLE;         // Atomic draw count

    VkDeviceMemory m_indirectMemory = VK_NULL_HANDLE;
    VkDeviceMemory m_visibleClusterMemory = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
    VkDeviceMemory m_cullingUniformMemory = VK_NULL_HANDLE;
    VkDeviceMemory m_drawCountMemory = VK_NULL_HANDLE;

    // Per-frame resources for GPU-driven mode (double buffered)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::array<PerFrameResources, MAX_FRAMES_IN_FLIGHT> m_frameResources;
    uint32_t m_currentFrame = 0;

    // Merged global buffers for GPU-driven rendering
    MergedMeshData m_mergedData;

    // Rendering uniform buffer
    VkBuffer m_renderUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_renderUniformMemory = VK_NULL_HANDLE;
    VGRenderUniforms m_renderUniforms;

    // Pipelines
    VkPipeline m_cullingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_cullingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_renderPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_renderPipelineLayout = VK_NULL_HANDLE;

    // Descriptor sets
    VkDescriptorSetLayout m_cullingDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_renderDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_cullingDescSet = VK_NULL_HANDLE;
    VkDescriptorSet m_renderDescSet = VK_NULL_HANDLE;

    // Light settings for rendering
    glm::vec3 m_lightDirection = glm::vec3(1.0f, -1.0f, 0.5f);
    glm::vec3 m_lightColor = glm::vec3(1.0f, 0.95f, 0.9f);
    float m_lightIntensity = 2.0f;
    float m_ambientIntensity = 0.1f;

    // Frame data
    glm::mat4 m_viewMatrix;
    glm::mat4 m_projMatrix;
    glm::vec3 m_cameraPosition;
    GPUCullingUniforms m_cullingUniforms;

    // Settings
    float m_lodBias = 1.0f;
    float m_errorThreshold = 1.0f;  // 1 pixel error threshold
    bool m_frustumCullingEnabled = true;
    bool m_lodSelectionEnabled = true;
    bool m_occlusionCullingEnabled = false;  // Hi-Z occlusion culling
    uint32_t m_debugMode = 0;  // 0=normal, 1=clusters, 2=normals, 3=LOD
    uint32_t m_forcedLodLevel = 0;  // Manual LOD level selection (0 = highest detail)
    bool m_gpuDrivenEnabled = false;  // GPU-driven indirect draw mode
    bool m_initialized = false;
    bool m_firstFrame = true;  // Track first frame for Hi-Z (no valid depth on first frame)

    // Hi-Z occlusion settings (adjustable in real-time)
    float m_hizMaxMipLevel = 3.0f;      // Max mip level (lower = more accurate, higher = faster)
    float m_hizDepthBias = 0.001f;      // Depth comparison bias (small value for precision at distance)
    float m_hizDepthThreshold = 0.999f; // "No occluder" threshold

    // Statistics
    uint32_t m_visibleClusterCount = 0;
    uint32_t m_totalClusterCount = 0;
    uint32_t m_drawCallCount = 0;

    // Descriptor pool (for culling descriptors)
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    // Hi-Z Occlusion Culling Resources
    VkImage m_hizImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hizMemory = VK_NULL_HANDLE;
    VkImageView m_hizImageView = VK_NULL_HANDLE;     // View for sampling all mips
    std::vector<VkImageView> m_hizMipViews;          // Per-mip views for compute writes
    VkSampler m_hizSampler = VK_NULL_HANDLE;
    uint32_t m_hizMipLevels = 0;
    uint32_t m_hizWidth = 0;
    uint32_t m_hizHeight = 0;

    // Hi-Z generation pipeline
    VkPipeline m_hizPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_hizPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_hizDescSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_hizDescSets;      // One per mip level transition

    // Hi-Z debug visualization
    VkPipeline m_hizDebugPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_hizDebugPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_hizDebugDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_hizDebugDescSet = VK_NULL_HANDLE;
    bool m_hizDebugEnabled = false;
    float m_hizDebugMipLevel = 0.0f;
    uint32_t m_hizDebugMode = 1;  // 0=grayscale, 1=heatmap

    // Hi-Z copy pass (graphics pass to copy depth to Hi-Z mip 0)
    // Using graphics pass because compute shaders can't reliably sample depth buffers
    VkRenderPass m_hizCopyRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_hizCopyFramebuffer = VK_NULL_HANDLE;
    VkPipeline m_hizCopyPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_hizCopyPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_hizCopyDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_hizCopyDescSet = VK_NULL_HANDLE;

    // Limits
    static constexpr uint32_t MAX_CLUSTERS = 1000000;    // 1M clusters
    static constexpr uint32_t MAX_INSTANCES = 10000;
    static constexpr uint32_t MAX_DRAWS = 100000;
};

} // namespace MiEngine
