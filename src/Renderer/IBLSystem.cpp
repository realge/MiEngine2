#include "Renderer/IBLSystem.h"


#include "VulkanRenderer.h"
#include "Utils/TextureUtils.h"
#include <iostream>

IBLSystem::IBLSystem(VulkanRenderer* renderer) : renderer(renderer) {
    // Initialize with null resources
    environmentMap = nullptr;
    irradianceMap = nullptr;
    prefilterMap = nullptr;
    brdfLUT = nullptr;
}

IBLSystem::~IBLSystem() {
    cleanup();
}

bool IBLSystem::initialize(const std::string& hdriPath) {
    // Reset state
    cleanup();
    this->currentHdriPath = hdriPath;
    VkDevice device = renderer->getDevice();
    VkPhysicalDevice physicalDevice = renderer->getPhysicalDevice();
    VkCommandPool commandPool = renderer->getCommandPool();
    VkQueue graphicsQueue = renderer->getGraphicsQueue();
    
    // Load environment map from HDRI
    std::cout << "Loading environment map from: " << hdriPath << std::endl;
    environmentMap = TextureUtils::createEnvironmentCubemap(
        device, 
        physicalDevice, 
        commandPool, 
        graphicsQueue, 
        hdriPath
    );
    
    if (!environmentMap) {
        std::cerr << "Failed to load environment map: " << hdriPath << std::endl;
        std::cout << "Creating default environment map instead" << std::endl;
        
        // Use default environment map if loading fails
        environmentMap = TextureUtils::createDefaultEnvironmentCubemap(
            device,
            physicalDevice,
            commandPool,
            graphicsQueue
        );
        
        if (!environmentMap) {
            std::cerr << "Failed to create default environment map" << std::endl;
            return false;
        }
    }
    
    // Create IBL resources
    bool success = createIBLResources();
    if (!success) {
        std::cerr << "Failed to create IBL resources" << std::endl;
        cleanup();
        return false;
    }
    
    // Create descriptor set layout
    descriptorSetLayout = createDescriptorSetLayout();
    if (descriptorSetLayout == VK_NULL_HANDLE) {
        std::cerr << "Failed to create IBL descriptor set layout" << std::endl;
        cleanup();
        return false;
    }
    
    // Create descriptor sets
    descriptorSets = createDescriptorSets(renderer->getDescriptorPool(), renderer->getMaxFramesInFlight());
    if (descriptorSets.empty()) {
        std::cerr << "Failed to create IBL descriptor sets" << std::endl;
        cleanup();
        return false;
    }
    
    initialized = true;
    std::cout << "IBL system initialized successfully" << std::endl;
    return true;
}



bool IBLSystem::createIBLResources() {
    if (!environmentMap) {
        std::cerr << "Environment map not loaded" << std::endl;
        return false;
    }

    // Read and cache the environment map data for CPU sampling
    std::cout << "Reading environment map data from GPU..." << std::endl;
    auto envData = TextureUtils::readCubemapFromGPU(
        renderer->getDevice(),
        renderer->getPhysicalDevice(),
        renderer->getCommandPool(),
        renderer->getGraphicsQueue(),
        environmentMap
    );
    
    if (envData) {
        TextureUtils::cacheEnvironmentMap(environmentMap, envData);
        TextureUtils::setCurrentEnvironmentData(envData);
        std::cout << "Environment map data cached for CPU sampling" << std::endl;
    } else {
        std::cerr << "Warning: Failed to cache environment map data" << std::endl;
    }
    
    VkDevice device = renderer->getDevice();
    VkPhysicalDevice physicalDevice = renderer->getPhysicalDevice();
    VkCommandPool commandPool = renderer->getCommandPool();
    VkQueue graphicsQueue = renderer->getGraphicsQueue();
    
    // Create irradiance map for diffuse lighting
    std::cout << "Creating irradiance map..." << std::endl;
    irradianceMap = TextureUtils::createIrradianceMap(
       device, physicalDevice, commandPool, graphicsQueue,
       environmentMap, currentHdriPath // <--- Pass Key
   );
    
    if (!irradianceMap) {
        std::cerr << "Failed to create irradiance map" << std::endl;
        return false;
    }
    
    // Create prefiltered environment map for specular reflections
    std::cout << "Creating prefiltered environment map..." << std::endl;
    prefilterMap = TextureUtils::createPrefilterMap(
       device, physicalDevice, commandPool, graphicsQueue,
       environmentMap, currentHdriPath // <--- Pass Key
   );
    
    if (!prefilterMap) {
        std::cerr << "Failed to create prefiltered environment map" << std::endl;
        return false;
    }
    
    // Create BRDF lookup table
    std::cout << "Creating BRDF lookup table..." << std::endl;
    brdfLUT = TextureUtils::createBRDFLookUpTexture(
        device,
        physicalDevice,
        commandPool,
        graphicsQueue,
        512  // Resolution of LUT
    );
    
    if (!brdfLUT) {
        std::cerr << "Failed to create BRDF lookup table" << std::endl;
        return false;
    }
    
    return true;
}

VkDescriptorSetLayout IBLSystem::createDescriptorSetLayout() {
    VkDevice device = renderer->getDevice();
    
    // Create bindings for IBL textures (2 cubemaps + 1 2D texture)
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    
    // Binding 0: Irradiance cubemap
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    // Binding 1: Prefiltered environment cubemap
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    
    // Binding 2: BRDF LUT
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        std::cerr << "Failed to create IBL descriptor set layout!" << std::endl;
        return VK_NULL_HANDLE;
    }
    
    return layout;
}

std::vector<VkDescriptorSet> IBLSystem::createDescriptorSets(VkDescriptorPool descriptorPool, uint32_t frameCount) {
    if (descriptorSetLayout == VK_NULL_HANDLE) {
        std::cerr << "Descriptor set layout not created" << std::endl;
        return {};
    }
    
    if (!irradianceMap || !prefilterMap || !brdfLUT) {
        std::cerr << "IBL textures not created" << std::endl;
        return {};
    }
    
    VkDevice device = renderer->getDevice();
    
    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(frameCount, descriptorSetLayout);
    std::vector<VkDescriptorSet> sets(frameCount);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = frameCount;
    allocInfo.pSetLayouts = layouts.data();
    
    if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate IBL descriptor sets!" << std::endl;
        return {};
    }
    
    // Update descriptor sets
    for (uint32_t i = 0; i < frameCount; i++) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        
        // Irradiance map
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = irradianceMap->getImageView();
        imageInfos[0].sampler = irradianceMap->getSampler();
        
        // Prefiltered environment map
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = prefilterMap->getImageView();
        imageInfos[1].sampler = prefilterMap->getSampler();
        
        // BRDF LUT
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = brdfLUT->getImageView();
        imageInfos[2].sampler = brdfLUT->getSampler();
        
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        // Irradiance map
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        
        // Prefiltered environment map
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];
        
        // BRDF LUT
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = sets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &imageInfos[2];
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    
    return sets;
}

void IBLSystem::cleanup() {
    VkDevice device = renderer->getDevice();
    
    // Cleanup descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    // Descriptor sets will be freed when the descriptor pool is destroyed
    descriptorSets.clear();
    
    // Reset textures (smart pointers will handle resource cleanup)
    environmentMap = nullptr;
    irradianceMap = nullptr;
    prefilterMap = nullptr;
    brdfLUT = nullptr;
    
    initialized = false;
}