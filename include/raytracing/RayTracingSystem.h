#pragma once

#include "RayTracingTypes.h"
#include <memory>
#include <unordered_map>
#include <functional>

// Forward declarations
class VulkanRenderer;
class Mesh;
class Scene;
class IBLSystem;

namespace MiEngine {

class MiWorld;

// ============================================================================
// RayTracingSystem - Main RT system managing acceleration structures and pipeline
// ============================================================================

class RayTracingSystem {
public:
    RayTracingSystem(VulkanRenderer* renderer);
    ~RayTracingSystem();

    // Prevent copying
    RayTracingSystem(const RayTracingSystem&) = delete;
    RayTracingSystem& operator=(const RayTracingSystem&) = delete;

    // ========================================================================
    // Initialization
    // ========================================================================

    // Check if RT is supported on current hardware
    static RTFeatureSupport checkSupport(VkPhysicalDevice physicalDevice);

    // Initialize the RT system (call after Vulkan device is created with RT features)
    bool initialize();

    // Cleanup all RT resources
    void cleanup();

    // Check if RT is ready to use
    bool isSupported() const { return m_FeatureSupport.supported; }
    bool isReady() const { return m_Initialized && m_FeatureSupport.supported; }
    const RTFeatureSupport& getFeatureSupport() const { return m_FeatureSupport; }

    // ========================================================================
    // Scene Management
    // ========================================================================

    // Update acceleration structures from legacy Scene
    void updateScene(Scene* scene);

    // Update acceleration structures from MiWorld
    void updateWorld(MiWorld* world);

    // Force rebuild of TLAS (call when scene changes significantly)
    void markTLASDirty() { m_TLASDirty = true; }

    // ========================================================================
    // Rendering
    // ========================================================================

    // Main ray tracing dispatch
    void traceRays(VkCommandBuffer commandBuffer,
                   const glm::mat4& view,
                   const glm::mat4& proj,
                   const glm::vec3& cameraPos,
                   uint32_t frameIndex);

    // Denoise the RT output
    void denoise(VkCommandBuffer commandBuffer, uint32_t frameIndex);

    // ========================================================================
    // Output Access
    // ========================================================================

    VkImageView getReflectionOutput() const { return m_ReflectionImageView; }
    VkImageView getShadowOutput() const { return m_ShadowImageView; }
    VkImageView getDenoisedOutput() const;

    VkDescriptorSet getOutputDescriptorSet(uint32_t frameIndex) const;
    VkDescriptorSetLayout getOutputDescriptorSetLayout() const { return m_OutputDescriptorSetLayout; }

    // ========================================================================
    // Settings
    // ========================================================================

    RTSettings& getSettings() { return m_Settings; }
    const RTSettings& getSettings() const { return m_Settings; }

    DenoiserSettings& getDenoiserSettings() { return m_DenoiserSettings; }
    const DenoiserSettings& getDenoiserSettings() const { return m_DenoiserSettings; }

    // ========================================================================
    // External System References
    // ========================================================================

    void setIBLSystem(IBLSystem* ibl) { m_IBLSystem = ibl; }

    // Set whether IBL is enabled (independent of IBL system being ready)
    void setIBLEnabled(bool enabled) { m_IBLEnabled = enabled; }
    bool isIBLEnabled() const { return m_IBLEnabled; }

    // Set G-buffer views for hybrid rendering
    void setGBufferViews(VkImageView depth, VkImageView normal, VkImageView metallicRoughness);

    // ========================================================================
    // Debug / Statistics
    // ========================================================================

    uint32_t getBLASCount() const { return static_cast<uint32_t>(m_BLASMap.size()); }
    uint32_t getTLASInstanceCount() const { return m_TLAS.instanceCount; }

    const RTPipelineProperties& getPipelineProperties() const { return m_PipelineProps; }
    const RTAccelerationStructureProperties& getASProperties() const { return m_ASProps; }

private:
    // ========================================================================
    // Internal Initialization
    // ========================================================================

    void loadExtensionFunctions();
    void queryRTProperties();
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createRTPipeline();
    bool createShaderBindingTable();
    bool createOutputImages();
    bool createUniformBuffers();
    bool createMaterialBuffer();
    bool createGeometryBuffers();
    void updateGeometryBuffers();

    // ========================================================================
    // Acceleration Structure Management
    // ========================================================================

    bool createBLAS(const std::shared_ptr<Mesh>& mesh, uint32_t meshId);
    void destroyBLAS(uint32_t meshId);
    const BLASInfo* getBLAS(uint32_t meshId) const;

    bool buildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances);
    bool updateTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances);

    void ensureScratchBuffer(VkDeviceSize requiredSize);
    VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer);

    // ========================================================================
    // Resource Management
    // ========================================================================

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory);
    bool createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    void updateDescriptorSets(uint32_t frameIndex);
    void updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view,
                            const glm::mat4& proj, const glm::vec3& cameraPos);

    // ========================================================================
    // Shader Loading
    // ========================================================================

    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readShaderFile(const std::string& filename);

private:
    VulkanRenderer* m_Renderer = nullptr;
    IBLSystem* m_IBLSystem = nullptr;
    bool m_IBLEnabled = true;  // Whether IBL is enabled (can be toggled by UI)

    bool m_Initialized = false;
    RTFeatureSupport m_FeatureSupport;
    RTSettings m_Settings;
    DenoiserSettings m_DenoiserSettings;

    // RT Properties
    RTPipelineProperties m_PipelineProps;
    RTAccelerationStructureProperties m_ASProps;

    // ========================================================================
    // Extension Function Pointers
    // ========================================================================

    PFN_vkGetAccelerationStructureBuildSizesKHR pfnGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR pfnCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR pfnDestroyAccelerationStructureKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR pfnCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR pfnCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR pfnGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR pfnCmdTraceRaysKHR = nullptr;
    PFN_vkGetBufferDeviceAddressKHR pfnGetBufferDeviceAddressKHR = nullptr;

    // ========================================================================
    // Acceleration Structures
    // ========================================================================

    std::unordered_map<uint32_t, BLASInfo> m_BLASMap;
    TLASInfo m_TLAS;
    bool m_TLASDirty = true;

    // Scratch buffer (reused for AS builds)
    VkBuffer m_ScratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_ScratchMemory = VK_NULL_HANDLE;
    VkDeviceSize m_ScratchSize = 0;

    // ========================================================================
    // RT Pipeline
    // ========================================================================

    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    // Shader Binding Table
    VkBuffer m_SBTBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_SBTMemory = VK_NULL_HANDLE;
    SBTRegion m_SBTRegions;

    // ========================================================================
    // Descriptors
    // ========================================================================

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;

    // Output descriptor set layout (for PBR shader to sample RT results)
    VkDescriptorSetLayout m_OutputDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_OutputDescriptorSets;

    // ========================================================================
    // Output Images
    // ========================================================================

    VkImage m_ReflectionImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ReflectionMemory = VK_NULL_HANDLE;
    VkImageView m_ReflectionImageView = VK_NULL_HANDLE;

    VkImage m_ShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ShadowMemory = VK_NULL_HANDLE;
    VkImageView m_ShadowImageView = VK_NULL_HANDLE;

    VkSampler m_OutputSampler = VK_NULL_HANDLE;

    uint32_t m_OutputWidth = 0;
    uint32_t m_OutputHeight = 0;

    // ========================================================================
    // G-Buffer References (from rasterization pass)
    // ========================================================================

    VkImageView m_GBufferDepth = VK_NULL_HANDLE;
    VkImageView m_GBufferNormal = VK_NULL_HANDLE;
    VkImageView m_GBufferMetallicRoughness = VK_NULL_HANDLE;

    // ========================================================================
    // Uniform Buffers
    // ========================================================================

    std::vector<VkBuffer> m_UniformBuffers;
    std::vector<VkDeviceMemory> m_UniformBuffersMemory;
    std::vector<void*> m_UniformBuffersMapped;

    // ========================================================================
    // Geometry Buffers (for bindless access)
    // ========================================================================

    VkBuffer m_GeometryVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_GeometryVertexMemory = VK_NULL_HANDLE;

    VkBuffer m_GeometryIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_GeometryIndexMemory = VK_NULL_HANDLE;

    VkBuffer m_InstanceDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_InstanceDataMemory = VK_NULL_HANDLE;
    void* m_InstanceDataMapped = nullptr;

    VkBuffer m_MaterialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_MaterialMemory = VK_NULL_HANDLE;
    void* m_MaterialBufferMapped = nullptr;
    uint32_t m_MaterialCount = 0;

    // Mesh info buffer (per-mesh vertex/index offsets)
    VkBuffer m_MeshInfoBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_MeshInfoMemory = VK_NULL_HANDLE;
    void* m_MeshInfoMapped = nullptr;

    // Global geometry tracking
    uint32_t m_TotalVertexCount = 0;
    uint32_t m_TotalIndexCount = 0;

    // ========================================================================
    // Frame tracking
    // ========================================================================

    uint32_t m_FrameNumber = 0;

    // ========================================================================
    // Denoiser Resources
    // ========================================================================

    // Compute pipelines for denoising
    VkPipeline m_TemporalDenoisePipeline = VK_NULL_HANDLE;
    VkPipeline m_SpatialDenoisePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_DenoisePipelineLayout = VK_NULL_HANDLE;

    // Denoiser descriptor sets
    VkDescriptorSetLayout m_DenoiseDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_DenoiseDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DenoiseDescriptorSets;        // For temporal pass
    std::vector<VkDescriptorSet> m_SpatialDenoiseDescriptorSets; // For spatial pass

    // History buffers for temporal accumulation
    VkImage m_HistoryReflectionImage = VK_NULL_HANDLE;
    VkDeviceMemory m_HistoryReflectionMemory = VK_NULL_HANDLE;
    VkImageView m_HistoryReflectionImageView = VK_NULL_HANDLE;

    VkImage m_HistoryShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_HistoryShadowMemory = VK_NULL_HANDLE;
    VkImageView m_HistoryShadowImageView = VK_NULL_HANDLE;

    // Intermediate buffer for ping-pong between temporal and spatial
    VkImage m_DenoisedReflectionImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DenoisedReflectionMemory = VK_NULL_HANDLE;
    VkImageView m_DenoisedReflectionImageView = VK_NULL_HANDLE;

    VkImage m_DenoisedShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DenoisedShadowMemory = VK_NULL_HANDLE;
    VkImageView m_DenoisedShadowImageView = VK_NULL_HANDLE;

    // Uniform buffer for denoiser settings
    std::vector<VkBuffer> m_DenoiseUniformBuffers;
    std::vector<VkDeviceMemory> m_DenoiseUniformBuffersMemory;
    std::vector<void*> m_DenoiseUniformBuffersMapped;

    // Previous frame matrices for reprojection
    glm::mat4 m_PrevViewProj = glm::mat4(1.0f);

    // Initialization helpers
    bool createDenoisePipelines();
    bool createDenoiseDescriptorSets();
    bool createHistoryBuffers();
    void updateDenoiseDescriptorSets(uint32_t frameIndex);
    void cleanupDenoiser();
};

} // namespace MiEngine
