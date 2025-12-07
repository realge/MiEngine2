#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "texture/Texture.h"

// Forward declarations
class VulkanRenderer;

/**
 * @brief Manages Image-Based Lighting resources and rendering
 * 
 * This class handles the creation and management of all IBL-related resources
 * including environment maps, irradiance maps, prefiltered environment maps,
 * and BRDF lookup tables. It works with the TextureUtils to generate these
 * resources and provides the interface for the renderer to use them.
 */
class IBLSystem {
public:
    /**
     * @brief Construct a new IBL System
     * 
     * @param renderer Pointer to the VulkanRenderer instance
     */
    IBLSystem(VulkanRenderer* renderer);
    
    /**
     * @brief Destroy the IBL System and free resources
     */
    ~IBLSystem();
    
    /**
     * @brief Initialize the IBL system with an environment map
     * 
     * @param hdriPath Path to the HDRI environment map
     * @return true if initialization succeeded
     * @return false if initialization failed
     */
    bool initialize(const std::string& hdriPath);
    
    /**
     * @brief Create descriptor set layout for IBL resources
     * 
     * @return VkDescriptorSetLayout The created descriptor set layout
     */
    VkDescriptorSetLayout createDescriptorSetLayout();
    
    /**
     * @brief Create and update descriptor sets for IBL resources
     * 
     * @param descriptorPool Descriptor pool to allocate from
     * @param frameCount Number of frames to create descriptor sets for
     * @return std::vector<VkDescriptorSet> The created descriptor sets
     */
    std::vector<VkDescriptorSet> createDescriptorSets(VkDescriptorPool descriptorPool, uint32_t frameCount);
    
    /**
     * @brief Check if the IBL system is ready for rendering
     * 
     * @return true if all resources are loaded and ready
     * @return false if resources are not ready
     */
    bool isReady() const { return initialized; }
    
    /**
     * @brief Get the environment map
     * 
     * @return std::shared_ptr<Texture> The environment map texture
     */
    std::shared_ptr<Texture> getEnvironmentMap() const { return environmentMap; }
    
    /**
     * @brief Get the irradiance map
     * 
     * @return std::shared_ptr<Texture> The irradiance map texture
     */
    std::shared_ptr<Texture> getIrradianceMap() const { return irradianceMap; }
    
    /**
     * @brief Get the prefiltered environment map
     * 
     * @return std::shared_ptr<Texture> The prefiltered environment map texture
     */
    std::shared_ptr<Texture> getPrefilterMap() const { return prefilterMap; }
    
    /**
     * @brief Get the BRDF lookup table
     * 
     * @return std::shared_ptr<Texture> The BRDF LUT texture
     */
    std::shared_ptr<Texture> getBrdfLUT() const { return brdfLUT; }
    
    /**
     * @brief Get the descriptor set layout
     * 
     * @return VkDescriptorSetLayout The descriptor set layout
     */
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    
    /**
     * @brief Get the descriptor sets
     * 
     * @return const std::vector<VkDescriptorSet>& The descriptor sets
     */
    const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }

private:
    // Pointer to the renderer
    VulkanRenderer* renderer;
    
    // IBL resources
    std::shared_ptr<Texture> environmentMap;    // HDR environment cubemap
    std::shared_ptr<Texture> irradianceMap;     // Diffuse irradiance cubemap
    std::shared_ptr<Texture> prefilterMap;      // Prefiltered environment map for specular
    std::shared_ptr<Texture> brdfLUT;           // BRDF lookup table
    
    // Vulkan resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    // State tracking
    bool initialized = false;

    std::string currentHdriPath;
    
    /**
     * @brief Create IBL resources from the environment map
     * 
     * @return true if creation succeeded
     * @return false if creation failed
     */
    bool createIBLResources();
    
    /**
     * @brief Cleanup all created resources
     */
    void cleanup();
};