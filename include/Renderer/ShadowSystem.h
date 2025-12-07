#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "scene/Scene.h"

// Forward declarations
class VulkanRenderer;
struct MeshInstance;

class ShadowSystem {
public:
    explicit ShadowSystem(VulkanRenderer* renderer);
    ~ShadowSystem();

    // Initialization and cleanup
    void initialize();
    void cleanup();

    // Shadow matrix calculation and updates
    void updateLightMatrix(const std::vector<Scene::Light>& lights, uint32_t frameIndex, const glm::vec3& cameraPosition);
    
    // Shadow pass rendering
    void renderShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances, uint32_t frameIndex);
    
    // Getters
    const glm::mat4& getLightSpaceMatrix() const { return lightSpaceMatrix; }
    VkImageView getShadowImageView() const { return shadowImageView; }
    VkSampler getShadowSampler() const { return shadowSampler; }
    
    // Configuration
    void setResolution(uint32_t width, uint32_t height);
    void setBias(float constantFactor, float slopeFactor);
    void setFrustumSize(float size);
    void setDepthRange(float nearPlane, float farPlane);

    // Getters for current values
    float getFrustumSize() const { return frustumSize; }
    float getDepthBiasConstant() const { return depthBiasConstant; }
    float getDepthBiasSlopeFactor() const { return depthBiasSlopeFactor; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }
    uint32_t getShadowMapWidth() const { return shadowMapWidth; }
    uint32_t getShadowMapHeight() const { return shadowMapHeight; }

    // Enable/disable shadows
    void setEnabled(bool enabled) { this->enabled = enabled; }
    bool isEnabled() const { return enabled; }

private:
    bool enabled = true;
    // Renderer reference
    VulkanRenderer* renderer;
    
    // Shadow map configuration
    uint32_t shadowMapWidth = 4096;
    uint32_t shadowMapHeight = 4096;
    float depthBiasConstant = 1.0f;
    float depthBiasSlopeFactor = 1.5f;
    float frustumSize = 50.0f;
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    
    // Shadow resources
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowImageMemory = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    
    // Shadow pipeline (static meshes)
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;

    // Skeletal shadow pipeline
    VkPipeline skeletalShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skeletalShadowPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skeletalShadowDescriptorSetLayout = VK_NULL_HANDLE;
    
    // Shadow uniform buffers (per frame in flight)
    struct ShadowUniformBuffer {
        glm::mat4 lightSpaceMatrix;
    };
    std::vector<VkBuffer> shadowUniformBuffers;
    std::vector<VkDeviceMemory> shadowUniformBuffersMemory;
    std::vector<void*> shadowUniformBuffersMapped;
    std::vector<VkDescriptorSet> shadowDescriptorSets;
    
    // Light space matrix
    glm::mat4 lightSpaceMatrix;
    
    // Resource creation methods
    void createShadowResources();
    void createShadowRenderPass();
    void createShadowPipeline();
    void createSkeletalShadowPipeline();
    void createShadowDescriptorResources();
    void createShadowFramebuffer();
    
    // Helper methods
    void updateShadowUniformBuffer(uint32_t frameIndex);
    glm::mat4 calculateLightSpaceMatrix(const glm::vec3& lightDirection, const glm::vec3& cameraPosition);
};
