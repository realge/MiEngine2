#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class VulkanRenderer;

namespace MiEngine {

// Frustum plane extraction from view-projection matrix
struct Frustum {
    glm::vec4 planes[6];  // Left, Right, Bottom, Top, Near, Far (xyz = normal, w = distance)

    void extractFromViewProj(const glm::mat4& viewProj);
    bool testSphere(const glm::vec3& center, float radius) const;
    bool testAABB(const glm::vec3& min, const glm::vec3& max) const;
};

// Bounding volume for culling
struct BoundingVolume {
    glm::vec3 sphereCenter;
    float sphereRadius;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

// GPU culling input (matches compute shader layout)
struct CullInputData {
    glm::vec4 sphereCenterRadius;  // xyz = center, w = radius
    glm::vec4 aabbMin;             // xyz = min, w = unused
    glm::vec4 aabbMax;             // xyz = max, w = unused
    uint32_t objectIndex;          // Original object index
    uint32_t padding[3];
};

// GPU culling uniform buffer
struct CullUniforms {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;      // xyz = position, w = unused
    uint32_t objectCount;
    uint32_t enableFrustumCull;
    uint32_t enableDistanceCull;
    float maxDrawDistance;
};

// Frustum culling system with CPU and GPU paths
class FrustumCulling {
public:
    FrustumCulling();
    ~FrustumCulling();

    bool initialize(VulkanRenderer* renderer);
    void cleanup();

    // CPU culling (for small object counts or fallback)
    void cullCPU(const Frustum& frustum,
                 const std::vector<BoundingVolume>& bounds,
                 std::vector<uint32_t>& visibleIndices);

    // GPU culling (for large object counts - Virtual Geo clusters)
    void cullGPU(VkCommandBuffer cmd,
                 uint32_t objectCount,
                 VkBuffer inputBuffer,      // CullInputData[]
                 VkBuffer outputBuffer,     // uint32_t[] visible indices
                 VkBuffer countBuffer);     // uint32_t visible count

    // Update frustum for current frame
    void updateFrustum(const glm::mat4& view, const glm::mat4& proj);

    // Accessors
    const Frustum& getCurrentFrustum() const { return m_Frustum; }
    bool isGPUCullingSupported() const { return m_GPUCullingReady; }

    // Settings
    void setMaxDrawDistance(float distance) { m_MaxDrawDistance = distance; }
    void setEnableFrustumCull(bool enable) { m_EnableFrustumCull = enable; }
    void setEnableDistanceCull(bool enable) { m_EnableDistanceCull = enable; }

private:
    void createComputePipeline();
    void createDescriptorSetLayout();
    void createUniformBuffer();
    void updateUniforms();

    VulkanRenderer* m_Renderer = nullptr;

    // Current frustum state
    Frustum m_Frustum;
    glm::mat4 m_ViewProj;
    glm::vec3 m_CameraPosition;

    // GPU resources
    bool m_GPUCullingReady = false;
    VkPipeline m_ComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

    // Uniform buffer (per-frame)
    std::vector<VkBuffer> m_UniformBuffers;
    std::vector<VkDeviceMemory> m_UniformMemory;
    std::vector<void*> m_UniformMapped;

    // Settings
    float m_MaxDrawDistance = 1000.0f;
    bool m_EnableFrustumCull = true;
    bool m_EnableDistanceCull = false;
};

} // namespace MiEngine
