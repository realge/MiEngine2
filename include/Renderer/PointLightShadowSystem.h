#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <memory>
#include "scene/Scene.h"

// Forward declarations
class VulkanRenderer;
struct MeshInstance;

// Maximum number of point lights that can cast shadows
#define MAX_SHADOW_POINT_LIGHTS 8

class PointLightShadowSystem {
public:
    explicit PointLightShadowSystem(VulkanRenderer* renderer);
    ~PointLightShadowSystem();

    // Initialization and cleanup
    void initialize();
    void cleanup();

    // Update shadow matrices for all point lights
    void updateLightMatrices(const std::vector<Scene::Light>& lights, uint32_t frameIndex);

    // Render shadow pass for all point lights (renders to cubemaps)
    void renderShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances, uint32_t frameIndex);

    // Getters for shader binding
    VkImageView getShadowCubeArrayView() const { return shadowCubeArrayView; }
    VkSampler getShadowSampler() const { return shadowSampler; }
    int getActiveShadowCount() const { return activeShadowLightCount; }

    // Get light info for shader (position and far plane per light)
    struct PointLightShadowInfo {
        glm::vec4 position;  // xyz = position, w = far plane
    };
    const std::vector<PointLightShadowInfo>& getShadowLightInfo() const { return shadowLightInfo; }

    // Configuration
    void setResolution(uint32_t size);
    void setBias(float constantFactor, float slopeFactor);
    void setNearFarPlanes(float nearPlane, float farPlane);

    // Enable/disable shadows
    void setEnabled(bool enabled) { this->enabled = enabled; }
    bool isEnabled() const { return enabled; }

private:
    bool enabled = true;
    // Renderer reference
    VulkanRenderer* renderer;

    // Shadow map configuration
    uint32_t shadowMapSize = 1024;  // Per face resolution (1024x1024)
    float depthBiasConstant = 1.25f;
    float depthBiasSlopeFactor = 1.75f;
    float nearPlane = 0.1f;
    float farPlane = 50.0f;  // Default far plane for point light shadows

    // Shadow cube array resources (all point light cubemaps in one array)
    VkImage shadowCubeArrayImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowCubeArrayMemory = VK_NULL_HANDLE;
    VkImageView shadowCubeArrayView = VK_NULL_HANDLE;  // Cube array view for shader sampling
    std::vector<VkImageView> shadowCubeFaceViews;      // Per-face views for rendering (6 * MAX_SHADOW_POINT_LIGHTS)
    VkSampler shadowSampler = VK_NULL_HANDLE;

    // Render pass and framebuffers
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> shadowFramebuffers;     // One per face per light (6 * MAX_SHADOW_POINT_LIGHTS)

    // Shadow pipeline
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;

    // Per-frame uniform buffers (contains all 6 view-proj matrices for current light)
    struct ShadowUniformBuffer {
        glm::mat4 lightViewProj[6];  // 6 face matrices
        glm::vec4 lightPos;          // xyz = position, w = far plane
    };
    std::vector<VkBuffer> shadowUniformBuffers;
    std::vector<VkDeviceMemory> shadowUniformBuffersMemory;
    std::vector<void*> shadowUniformBuffersMapped;
    std::vector<VkDescriptorSet> shadowDescriptorSets;
    VkDeviceSize dynamicAlignment = 0;

    // Active shadow casting lights info
    int activeShadowLightCount = 0;
    std::vector<PointLightShadowInfo> shadowLightInfo;

    // Light view-projection matrices (cached per frame)
    std::array<glm::mat4, 6> calculateCubeFaceMatrices(const glm::vec3& lightPos, float farPlane);

    // Resource creation methods
    void createShadowCubeArray();
    void createShadowRenderPass();
    void createShadowPipeline();
    void createShadowDescriptorResources();
    void createShadowFramebuffers();

    // Helper methods
    void updateShadowUniformBuffer(uint32_t frameIndex, int lightIndex, const glm::vec3& lightPos);
    void renderLightShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances,
                               uint32_t frameIndex, int lightIndex);
};
