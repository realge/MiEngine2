#include "Renderer/WaterSystem.h"
#include "VulkanRenderer.h"
#include "Renderer/IBLSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <iostream>
#include <array>
#include <cstring>

WaterSystem::WaterSystem(VulkanRenderer* renderer)
    : renderer(renderer) {
}

WaterSystem::~WaterSystem() {
    cleanup();
}

bool WaterSystem::initialize(uint32_t resolution) {
    std::cout << "Initializing Water System..." << std::endl;

    gridResolution = resolution;

    try {
        createHeightMaps();
        createNormalMap();
        createSamplers();
        createWaterMesh();
        createUniformBuffers();
        createRippleBuffer();
        createDescriptorPool();
        createComputeDescriptorSetLayouts();
        createComputePipelines();
        createGraphicsDescriptorSetLayout();
        createGraphicsPipeline();
        createDescriptorSets();

        initialized = true;
        std::cout << "Water System initialized successfully" << std::endl;

        // Initial test ripple disabled - use debug panel to add ripples manually
        // addRipple(glm::vec2(0.5f, 0.5f), 0.8f, 0.005f);

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to initialize Water System: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

void WaterSystem::cleanup() {
    VkDevice device = renderer->getDevice();

    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(device);

    // Cleanup graphics pipeline
    if (waterGraphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, waterGraphicsPipeline, nullptr);
        waterGraphicsPipeline = VK_NULL_HANDLE;
    }
    if (waterGraphicsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, waterGraphicsPipelineLayout, nullptr);
        waterGraphicsPipelineLayout = VK_NULL_HANDLE;
    }
    if (waterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, waterDescriptorSetLayout, nullptr);
        waterDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup compute pipelines
    if (waveComputePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, waveComputePipeline, nullptr);
        waveComputePipeline = VK_NULL_HANDLE;
    }
    if (waveComputePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, waveComputePipelineLayout, nullptr);
        waveComputePipelineLayout = VK_NULL_HANDLE;
    }
    if (waveComputeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, waveComputeDescriptorSetLayout, nullptr);
        waveComputeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (normalComputePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, normalComputePipeline, nullptr);
        normalComputePipeline = VK_NULL_HANDLE;
    }
    if (normalComputePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, normalComputePipelineLayout, nullptr);
        normalComputePipelineLayout = VK_NULL_HANDLE;
    }
    if (normalComputeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, normalComputeDescriptorSetLayout, nullptr);
        normalComputeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup descriptor pool
    if (waterDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, waterDescriptorPool, nullptr);
        waterDescriptorPool = VK_NULL_HANDLE;
    }

    // Cleanup uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffersMapped[i]) {
            vkUnmapMemory(device, uniformBuffersMemory[i]);
        }
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        }
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        }
    }
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();

    // Cleanup ripple buffer
    if (rippleBufferMapped) {
        vkUnmapMemory(device, rippleBufferMemory);
        rippleBufferMapped = nullptr;
    }
    if (rippleBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, rippleBuffer, nullptr);
        rippleBuffer = VK_NULL_HANDLE;
    }
    if (rippleBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, rippleBufferMemory, nullptr);
        rippleBufferMemory = VK_NULL_HANDLE;
    }

    // Cleanup mesh buffers
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer, nullptr);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory, nullptr);
        indexBufferMemory = VK_NULL_HANDLE;
    }

    // Cleanup samplers
    if (heightMapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, heightMapSampler, nullptr);
        heightMapSampler = VK_NULL_HANDLE;
    }
    if (normalMapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, normalMapSampler, nullptr);
        normalMapSampler = VK_NULL_HANDLE;
    }

    // Cleanup normal map
    if (normalMapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, normalMapView, nullptr);
        normalMapView = VK_NULL_HANDLE;
    }
    if (normalMap != VK_NULL_HANDLE) {
        vkDestroyImage(device, normalMap, nullptr);
        normalMap = VK_NULL_HANDLE;
    }
    if (normalMapMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, normalMapMemory, nullptr);
        normalMapMemory = VK_NULL_HANDLE;
    }

    // Cleanup height maps (3 buffers)
    for (int i = 0; i < 3; i++) {
        if (heightMapViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, heightMapViews[i], nullptr);
            heightMapViews[i] = VK_NULL_HANDLE;
        }
        if (heightMaps[i] != VK_NULL_HANDLE) {
            vkDestroyImage(device, heightMaps[i], nullptr);
            heightMaps[i] = VK_NULL_HANDLE;
        }
        if (heightMapMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, heightMapMemory[i], nullptr);
            heightMapMemory[i] = VK_NULL_HANDLE;
        }
    }

    initialized = false;
}

void WaterSystem::createHeightMaps() {
    VkDevice device = renderer->getDevice();

    // Create 3 height map buffers for proper wave equation (previous, current, output)
    for (int i = 0; i < 3; i++) {
        // Create height map image (R32_SFLOAT for float heights)
        renderer->createImage(
            gridResolution, gridResolution,
            VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            heightMaps[i], heightMapMemory[i]
        );

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = heightMaps[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &heightMapViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create height map image view!");
        }
    }

    // Initialize buffer indices: previous=0, current=1, output=2
    previousHeightMap = 0;
    currentHeightMap = 1;
    outputHeightMap = 2;

    // Transition height maps to general layout for compute shader access
    VkCommandBuffer cmdBuffer = renderer->beginSingleTimeCommands();
    for (int i = 0; i < 3; i++) {
        transitionImageLayout(cmdBuffer, heightMaps[i],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    renderer->endSingleTimeCommands(cmdBuffer);
}

void WaterSystem::createNormalMap() {
    VkDevice device = renderer->getDevice();

    // Create normal map image (RGBA8 for normal storage)
    renderer->createImage(
        gridResolution, gridResolution,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        normalMap, normalMapMemory
    );

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = normalMap;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &normalMapView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create normal map image view!");
    }

    // Transition to general layout
    VkCommandBuffer cmdBuffer = renderer->beginSingleTimeCommands();
    transitionImageLayout(cmdBuffer, normalMap,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    renderer->endSingleTimeCommands(cmdBuffer);
}

void WaterSystem::createSamplers() {
    VkDevice device = renderer->getDevice();

    // Height map sampler (linear filtering, clamp to edge)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &heightMapSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create height map sampler!");
    }

    // Normal map sampler (same settings)
    if (vkCreateSampler(device, &samplerInfo, nullptr, &normalMapSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create normal map sampler!");
    }
}

void WaterSystem::createWaterMesh() {
    VkDevice device = renderer->getDevice();

    // Create a grid mesh for water surface
    // Simple vertex format: position (x, y, z) + texcoord (u, v)
    struct WaterVertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    std::vector<WaterVertex> vertices;
    std::vector<uint32_t> indices;

    // Generate grid vertices
    float halfSize = 0.5f;  // Mesh from -0.5 to 0.5, scaled by transform
    for (uint32_t z = 0; z <= meshResolution; z++) {
        for (uint32_t x = 0; x <= meshResolution; x++) {
            WaterVertex vertex;
            vertex.position.x = -halfSize + (float)x / meshResolution;
            vertex.position.y = 0.0f;
            vertex.position.z = -halfSize + (float)z / meshResolution;
            vertex.texCoord.x = (float)x / meshResolution;
            vertex.texCoord.y = (float)z / meshResolution;
            vertices.push_back(vertex);
        }
    }

    // Generate indices for triangle strip
    for (uint32_t z = 0; z < meshResolution; z++) {
        for (uint32_t x = 0; x < meshResolution; x++) {
            uint32_t topLeft = z * (meshResolution + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (meshResolution + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(WaterVertex) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertices.data(), vertexBufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    renderer->createBuffer(vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer, vertexBufferMemory);

    renderer->copyBuffer(stagingBuffer, vertexBuffer, vertexBufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // Create index buffer
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    renderer->createBuffer(indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    vkMapMemory(device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, indices.data(), indexBufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    renderer->createBuffer(indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indexBuffer, indexBufferMemory);

    renderer->copyBuffer(stagingBuffer, indexBuffer, indexBufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    std::cout << "Water mesh created: " << vertexCount << " vertices, " << indexCount << " indices" << std::endl;
}

void WaterSystem::createUniformBuffers() {
    uint32_t maxFrames = renderer->getMaxFramesInFlight();
    VkDeviceSize bufferSize = sizeof(WaterUniformBuffer);

    uniformBuffers.resize(maxFrames);
    uniformBuffersMemory.resize(maxFrames);
    uniformBuffersMapped.resize(maxFrames);

    for (size_t i = 0; i < maxFrames; i++) {
        renderer->createBuffer(bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniformBuffers[i], uniformBuffersMemory[i]);

        vkMapMemory(renderer->getDevice(), uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void WaterSystem::createRippleBuffer() {
    VkDeviceSize bufferSize = sizeof(RippleBuffer);

    renderer->createBuffer(bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        rippleBuffer, rippleBufferMemory);

    vkMapMemory(renderer->getDevice(), rippleBufferMemory, 0, bufferSize, 0, &rippleBufferMapped);

    // Initialize with zero ripples
    RippleBuffer initialData{};
    initialData.rippleCount = 0;
    memcpy(rippleBufferMapped, &initialData, sizeof(RippleBuffer));
}

void WaterSystem::createDescriptorPool() {
    uint32_t maxFrames = renderer->getMaxFramesInFlight();

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    // Storage images for compute shaders (3 height maps + 1 normal map, used in multiple sets)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = maxFrames * 4;  // 3 height maps + normal map for multiple descriptor sets per frame
    // Combined image samplers for graphics shader
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = maxFrames * 3;
    // Uniform buffers
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = maxFrames + 5;  // Water UBO + ripple buffer

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;  // Standard descriptor pool
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFrames * 3 + 15;  // Graphics sets + compute sets (wave + normal) per frame

    if (vkCreateDescriptorPool(renderer->getDevice(), &poolInfo, nullptr, &waterDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water descriptor pool!");
    }
}

void WaterSystem::createComputeDescriptorSetLayouts() {
    VkDevice device = renderer->getDevice();

    // Wave simulation descriptor set layout
    // Binding 0: Current height map (storage image, read)
    // Binding 1: Previous height map (storage image, read)
    // Binding 2: Output height map (storage image, write)
    // Binding 3: Ripple buffer (uniform buffer)
    std::array<VkDescriptorSetLayoutBinding, 4> waveBindings{};

    waveBindings[0].binding = 0;
    waveBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    waveBindings[0].descriptorCount = 1;
    waveBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    waveBindings[1].binding = 1;
    waveBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    waveBindings[1].descriptorCount = 1;
    waveBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    waveBindings[2].binding = 2;
    waveBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    waveBindings[2].descriptorCount = 1;
    waveBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    waveBindings[3].binding = 3;
    waveBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    waveBindings[3].descriptorCount = 1;
    waveBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo waveLayoutInfo{};
    waveLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    waveLayoutInfo.bindingCount = static_cast<uint32_t>(waveBindings.size());
    waveLayoutInfo.pBindings = waveBindings.data();

    if (vkCreateDescriptorSetLayout(device, &waveLayoutInfo, nullptr, &waveComputeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create wave compute descriptor set layout!");
    }

    // Normal generation descriptor set layout
    // Binding 0: Height map (storage image, read)
    // Binding 1: Normal map (storage image, write)
    std::array<VkDescriptorSetLayoutBinding, 2> normalBindings{};

    normalBindings[0].binding = 0;
    normalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    normalBindings[0].descriptorCount = 1;
    normalBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    normalBindings[1].binding = 1;
    normalBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    normalBindings[1].descriptorCount = 1;
    normalBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo normalLayoutInfo{};
    normalLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    normalLayoutInfo.bindingCount = static_cast<uint32_t>(normalBindings.size());
    normalLayoutInfo.pBindings = normalBindings.data();

    if (vkCreateDescriptorSetLayout(device, &normalLayoutInfo, nullptr, &normalComputeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create normal compute descriptor set layout!");
    }
}

void WaterSystem::createComputePipelines() {
    VkDevice device = renderer->getDevice();

    // Wave simulation pipeline
    {
        auto compCode = renderer->readFile("shaders/water_simulation.comp.spv");
        VkShaderModule compModule = renderer->createShaderModule(compCode);

        VkPipelineShaderStageCreateInfo compStage{};
        compStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compStage.module = compModule;
        compStage.pName = "main";

        // Push constants for simulation parameters
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(WaveSimulationPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &waveComputeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &waveComputePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create wave compute pipeline layout!");
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = compStage;
        pipelineInfo.layout = waveComputePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &waveComputePipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create wave compute pipeline!");
        }

        vkDestroyShaderModule(device, compModule, nullptr);
    }

    // Normal generation pipeline
    {
        auto compCode = renderer->readFile("shaders/water_normals.comp.spv");
        VkShaderModule compModule = renderer->createShaderModule(compCode);

        VkPipelineShaderStageCreateInfo compStage{};
        compStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compStage.module = compModule;
        compStage.pName = "main";

        // Push constants for normal generation parameters
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(NormalGenerationPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &normalComputeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &normalComputePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal compute pipeline layout!");
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = compStage;
        pipelineInfo.layout = normalComputePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &normalComputePipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal compute pipeline!");
        }

        vkDestroyShaderModule(device, compModule, nullptr);
    }

    std::cout << "Water compute pipelines created successfully" << std::endl;
}

void WaterSystem::createGraphicsDescriptorSetLayout() {
    VkDevice device = renderer->getDevice();

    // Water graphics descriptor set layout
    // Binding 0: Water uniform buffer
    // Binding 1: Height map sampler
    // Binding 2: Normal map sampler
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &waterDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water graphics descriptor set layout!");
    }
}

void WaterSystem::createGraphicsPipeline() {
    VkDevice device = renderer->getDevice();

    // Load shaders
    auto vertCode = renderer->readFile("shaders/water.vert.spv");
    auto fragCode = renderer->readFile("shaders/water.frag.spv");

    VkShaderModule vertModule = renderer->createShaderModule(vertCode);
    VkShaderModule fragModule = renderer->createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    // Vertex input - custom water vertex format
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(glm::vec3) + sizeof(glm::vec2);  // position + texcoord
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrDescs{};
    attrDescs[0].binding = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset = 0;

    attrDescs[1].binding = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[1].offset = sizeof(glm::vec3);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Render both sides of water
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (alpha blending for water transparency)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic states
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // Pipeline layout - uses two descriptor set layouts:
    // Set 0: Water uniforms (UBO, height map, normal map)
    // Set 1: IBL textures (irradiance, prefilter, brdfLUT) - reuses existing IBL layout
    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.push_back(waterDescriptorSetLayout);

    // Get IBL descriptor set layout from IBLSystem if available
    IBLSystem* iblSystem = renderer->getIBLSystem();
    if (iblSystem && iblSystem->isReady()) {
        setLayouts.push_back(iblSystem->getDescriptorSetLayout());
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &waterGraphicsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water graphics pipeline layout!");
    }

    // Get render pass from renderer
    VkRenderPass mainRenderPass = renderer->getRenderPass();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = waterGraphicsPipelineLayout;
    pipelineInfo.renderPass = mainRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &waterGraphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water graphics pipeline!");
    }

    // Log whether IBL was included
    IBLSystem* iblCheck = renderer->getIBLSystem();
    if (iblCheck && iblCheck->isReady()) {
        std::cout << "Water graphics pipeline created with IBL support (2 descriptor sets)" << std::endl;
    } else {
        std::cout << "Water graphics pipeline created WITHOUT IBL (1 descriptor set only)" << std::endl;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

void WaterSystem::recreateGraphicsPipeline() {
    if (!initialized) return;

    VkDevice device = renderer->getDevice();
    vkDeviceWaitIdle(device);

    // Cleanup old pipeline and layout
    if (waterGraphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, waterGraphicsPipeline, nullptr);
        waterGraphicsPipeline = VK_NULL_HANDLE;
    }
    if (waterGraphicsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, waterGraphicsPipelineLayout, nullptr);
        waterGraphicsPipelineLayout = VK_NULL_HANDLE;
    }

    // Recreate with current IBL state
    createGraphicsPipeline();
}

void WaterSystem::createDescriptorSets() {
    VkDevice device = renderer->getDevice();
    uint32_t maxFrames = renderer->getMaxFramesInFlight();

    // Create wave compute descriptor sets (one per frame in flight)
    {
        std::vector<VkDescriptorSetLayout> layouts(maxFrames, waveComputeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = waterDescriptorPool;
        allocInfo.descriptorSetCount = maxFrames;
        allocInfo.pSetLayouts = layouts.data();

        waveComputeDescriptorSets.resize(maxFrames);
        if (vkAllocateDescriptorSets(device, &allocInfo, waveComputeDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate wave compute descriptor sets!");
        }

        // Initial update for all frames
        for (uint32_t i = 0; i < maxFrames; i++) {
            VkDescriptorImageInfo currentHeightInfo{};
            currentHeightInfo.imageView = heightMapViews[currentHeightMap];
            currentHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo prevHeightInfo{};
            prevHeightInfo.imageView = heightMapViews[previousHeightMap];
            prevHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo outHeightInfo{};
            outHeightInfo.imageView = heightMapViews[outputHeightMap];
            outHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorBufferInfo rippleInfo{};
            rippleInfo.buffer = rippleBuffer;
            rippleInfo.offset = 0;
            rippleInfo.range = sizeof(RippleBuffer);

            std::array<VkWriteDescriptorSet, 4> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = waveComputeDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &currentHeightInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = waveComputeDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &prevHeightInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = waveComputeDescriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &outHeightInfo;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = waveComputeDescriptorSets[i];
            writes[3].dstBinding = 3;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &rippleInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Create normal compute descriptor sets (one per frame in flight)
    {
        std::vector<VkDescriptorSetLayout> layouts(maxFrames, normalComputeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = waterDescriptorPool;
        allocInfo.descriptorSetCount = maxFrames;
        allocInfo.pSetLayouts = layouts.data();

        normalComputeDescriptorSets.resize(maxFrames);
        if (vkAllocateDescriptorSets(device, &allocInfo, normalComputeDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate normal compute descriptor sets!");
        }

        // Initial update for all frames
        for (uint32_t i = 0; i < maxFrames; i++) {
            VkDescriptorImageInfo heightInfo{};
            heightInfo.imageView = heightMapViews[outputHeightMap];
            heightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo normalInfo{};
            normalInfo.imageView = normalMapView;
            normalInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::array<VkWriteDescriptorSet, 2> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = normalComputeDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &heightInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = normalComputeDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &normalInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Create graphics descriptor sets (one per frame in flight)
    {
        std::vector<VkDescriptorSetLayout> layouts(maxFrames, waterDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = waterDescriptorPool;
        allocInfo.descriptorSetCount = maxFrames;
        allocInfo.pSetLayouts = layouts.data();

        waterDescriptorSets.resize(maxFrames);
        if (vkAllocateDescriptorSets(device, &allocInfo, waterDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate water graphics descriptor sets!");
        }

        // Update descriptor sets
        for (uint32_t i = 0; i < maxFrames; i++) {
            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = uniformBuffers[i];
            uboInfo.offset = 0;
            uboInfo.range = sizeof(WaterUniformBuffer);

            VkDescriptorImageInfo heightInfo{};
            heightInfo.sampler = heightMapSampler;
            heightInfo.imageView = heightMapViews[0];  // Will be updated dynamically
            heightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo normalInfo{};
            normalInfo.sampler = normalMapSampler;
            normalInfo.imageView = normalMapView;
            normalInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::array<VkWriteDescriptorSet, 3> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = waterDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &uboInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = waterDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &heightInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = waterDescriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &normalInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::cout << "Water descriptor sets created" << std::endl;
}

void WaterSystem::update(VkCommandBuffer commandBuffer, float deltaTime, uint32_t frameIndex) {
    if (!initialized) return;

    accumulatedTime += deltaTime;

    // Inject pending ripples
    injectRipples();

    // Dispatch wave simulation
    dispatchWaveSimulation(commandBuffer, deltaTime, frameIndex);

    // Dispatch normal generation
    dispatchNormalGeneration(commandBuffer, frameIndex);

    // Swap height maps for next frame
    swapHeightMaps();
}

void WaterSystem::addRipple(const glm::vec2& position, float strength, float radius) {
    if (pendingRipples.size() < 16) {
        pendingRipples.push_back({ position, strength, radius });
    }
}

void WaterSystem::render(VkCommandBuffer commandBuffer,
                         const glm::mat4& view,
                         const glm::mat4& projection,
                         const glm::vec3& cameraPos,
                         uint32_t frameIndex) {
    if (!initialized || waterGraphicsPipeline == VK_NULL_HANDLE) return;

    VkDevice device = renderer->getDevice();

    // Update uniform buffer
    WaterUniformBuffer ubo{};
    ubo.model = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), scale);
    ubo.view = view;
    ubo.projection = projection;
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.shallowColor = glm::vec4(parameters.shallowColor, 1.0f);
    ubo.deepColor = glm::vec4(parameters.deepColor, 1.0f);
    ubo.time = accumulatedTime;
    ubo.heightScale = parameters.heightScale;
    ubo.gridSize = static_cast<float>(gridResolution);
    ubo.fresnelPower = parameters.fresnelPower;
    ubo.reflectionStrength = parameters.reflectionStrength;
    ubo.specularPower = parameters.specularPower;

    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(WaterUniformBuffer));

    // Update height map binding to use the current (most recent) height map
    // After swap: outputHeightMap has the newest data, which becomes currentHeightMap
    VkDescriptorImageInfo heightInfo{};
    heightInfo.sampler = heightMapSampler;
    heightInfo.imageView = heightMapViews[currentHeightMap];
    heightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = waterDescriptorSets[frameIndex];
    write.dstBinding = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &heightInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Bind pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, waterGraphicsPipeline);

    // Bind water descriptor set (Set 0)
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        waterGraphicsPipelineLayout, 0, 1, &waterDescriptorSets[frameIndex], 0, nullptr);

    // Bind IBL descriptor set (Set 1) if IBL is available
    IBLSystem* iblSystem = renderer->getIBLSystem();
    if (iblSystem && iblSystem->isReady()) {
        const std::vector<VkDescriptorSet>& iblSets = iblSystem->getDescriptorSets();
        if (frameIndex < iblSets.size() && iblSets[frameIndex] != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                waterGraphicsPipelineLayout, 1, 1, &iblSets[frameIndex], 0, nullptr);
        } else {
            static bool warnedOnce = false;
            if (!warnedOnce) {
                std::cerr << "WARNING: IBL descriptor set is NULL or size mismatch! frameIndex=" << frameIndex << " size=" << iblSets.size() << std::endl;
                warnedOnce = true;
            }
        }
    } else {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            std::cerr << "WARNING: IBL not ready for water rendering! iblSystem=" << (iblSystem ? "exists" : "null")
                      << " isReady=" << (iblSystem ? (iblSystem->isReady() ? "true" : "false") : "n/a") << std::endl;
            warnedOnce = true;
        }
    }

    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);

    // Track render statistics
    renderer->addDrawCall(0, indexCount);
}

void WaterSystem::dispatchWaveSimulation(VkCommandBuffer commandBuffer, float deltaTime, uint32_t frameIndex) {
    VkDevice device = renderer->getDevice();

    // Update wave compute descriptor set with current buffer configuration
    VkDescriptorImageInfo currentHeightInfo{};
    currentHeightInfo.imageView = heightMapViews[currentHeightMap];
    currentHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo prevHeightInfo{};
    prevHeightInfo.imageView = heightMapViews[previousHeightMap];
    prevHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo outHeightInfo{};
    outHeightInfo.imageView = heightMapViews[outputHeightMap];
    outHeightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 3> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = waveComputeDescriptorSets[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &currentHeightInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = waveComputeDescriptorSets[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &prevHeightInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = waveComputeDescriptorSets[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &outHeightInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Bind compute pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, waveComputePipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        waveComputePipelineLayout, 0, 1, &waveComputeDescriptorSets[frameIndex], 0, nullptr);

    // Push constants
    WaveSimulationPushConstants pushConstants{};
    pushConstants.deltaTime = deltaTime;
    pushConstants.waveSpeed = parameters.waveSpeed;
    pushConstants.damping = parameters.damping;
    pushConstants.gridSize = static_cast<int>(gridResolution);

    vkCmdPushConstants(commandBuffer, waveComputePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WaveSimulationPushConstants), &pushConstants);

    // Dispatch compute shader
    uint32_t groupCount = (gridResolution + 15) / 16;
    vkCmdDispatch(commandBuffer, groupCount, groupCount, 1);

    // Memory barrier to ensure compute shader writes are visible
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void WaterSystem::dispatchNormalGeneration(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    VkDevice device = renderer->getDevice();

    // Update normal compute descriptor set - read from output buffer (new height data)
    VkDescriptorImageInfo heightInfo{};
    heightInfo.imageView = heightMapViews[outputHeightMap];
    heightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = normalComputeDescriptorSets[frameIndex];
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &heightInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Bind compute pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, normalComputePipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        normalComputePipelineLayout, 0, 1, &normalComputeDescriptorSets[frameIndex], 0, nullptr);

    // Push constants
    NormalGenerationPushConstants pushConstants{};
    pushConstants.gridSize = static_cast<int>(gridResolution);
    pushConstants.heightScale = parameters.heightScale;
    pushConstants.texelSize = 1.0f / static_cast<float>(gridResolution);

    vkCmdPushConstants(commandBuffer, normalComputePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalGenerationPushConstants), &pushConstants);

    // Dispatch compute shader
    uint32_t groupCount = (gridResolution + 15) / 16;
    vkCmdDispatch(commandBuffer, groupCount, groupCount, 1);

    // Memory barrier for vertex shader to read
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void WaterSystem::injectRipples() {
    RippleBuffer data{};
    data.rippleCount = static_cast<int>(pendingRipples.size());

    for (size_t i = 0; i < pendingRipples.size() && i < 16; i++) {
        data.ripples[i] = glm::vec4(
            pendingRipples[i].position.x,
            pendingRipples[i].position.y,
            pendingRipples[i].strength,
            pendingRipples[i].radius
        );
    }

    memcpy(rippleBufferMapped, &data, sizeof(RippleBuffer));
    pendingRipples.clear();
}

void WaterSystem::swapHeightMaps() {
    // Rotate buffers: output -> current -> previous
    // After wave simulation: output has new data (t+1)
    // For next frame: output becomes current, current becomes previous, previous becomes output
    int oldPrevious = previousHeightMap;
    previousHeightMap = currentHeightMap;
    currentHeightMap = outputHeightMap;
    outputHeightMap = oldPrevious;
}

void WaterSystem::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                                        VkImageLayout oldLayout, VkImageLayout newLayout,
                                        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(commandBuffer,
        srcStage, dstStage,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}
