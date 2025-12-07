#include "Renderer/PointLightShadowSystem.h"
#include "VulkanRenderer.h"
#include "scene/Scene.h"
#include "Utils/CommonVertex.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <iostream>
#include <array>

PointLightShadowSystem::PointLightShadowSystem(VulkanRenderer* renderer)
    : renderer(renderer) {
    shadowLightInfo.resize(MAX_SHADOW_POINT_LIGHTS);
}

PointLightShadowSystem::~PointLightShadowSystem() {
    cleanup();
}

void PointLightShadowSystem::initialize() {
    std::cout << "Initializing Point Light Shadow System..." << std::endl;

    // Calculate dynamic alignment
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(renderer->getPhysicalDevice(), &properties);
    VkDeviceSize minAlignment = properties.limits.minUniformBufferOffsetAlignment;
    
    dynamicAlignment = sizeof(ShadowUniformBuffer);
    if (minAlignment > 0) {
        dynamicAlignment = (dynamicAlignment + minAlignment - 1) & ~(minAlignment - 1);
    }

    createShadowCubeArray();
    createShadowRenderPass();
    createShadowDescriptorResources();
    createShadowPipeline();
    createShadowFramebuffers();

    std::cout << "Point Light Shadow System initialized successfully" << std::endl;
}

void PointLightShadowSystem::cleanup() {
    VkDevice device = renderer->getDevice();

    // Cleanup pipeline
    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, shadowPipeline, nullptr);
        shadowPipeline = VK_NULL_HANDLE;
    }

    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        shadowPipelineLayout = VK_NULL_HANDLE;
    }

    // Cleanup framebuffers
    for (auto fb : shadowFramebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    shadowFramebuffers.clear();

    // Cleanup render pass
    if (shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, shadowRenderPass, nullptr);
        shadowRenderPass = VK_NULL_HANDLE;
    }

    // Cleanup descriptor resources
    if (shadowDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, shadowDescriptorSetLayout, nullptr);
        shadowDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup uniform buffers
    for (size_t i = 0; i < shadowUniformBuffers.size(); i++) {
        if (shadowUniformBuffersMapped[i]) {
            vkUnmapMemory(device, shadowUniformBuffersMemory[i]);
        }
        if (shadowUniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, shadowUniformBuffers[i], nullptr);
        }
        if (shadowUniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, shadowUniformBuffersMemory[i], nullptr);
        }
    }
    shadowUniformBuffers.clear();
    shadowUniformBuffersMemory.clear();
    shadowUniformBuffersMapped.clear();
    shadowDescriptorSets.clear();

    // Cleanup sampler
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }

    // Cleanup cube face views
    for (auto view : shadowCubeFaceViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    shadowCubeFaceViews.clear();

    // Cleanup cube array view
    if (shadowCubeArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowCubeArrayView, nullptr);
        shadowCubeArrayView = VK_NULL_HANDLE;
    }

    // Cleanup cube array image
    if (shadowCubeArrayImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, shadowCubeArrayImage, nullptr);
        shadowCubeArrayImage = VK_NULL_HANDLE;
    }

    if (shadowCubeArrayMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, shadowCubeArrayMemory, nullptr);
        shadowCubeArrayMemory = VK_NULL_HANDLE;
    }
}

void PointLightShadowSystem::createShadowCubeArray() {
    VkDevice device = renderer->getDevice();
    VkFormat depthFormat = renderer->findDepthFormat();

    // Create cube map array image (6 faces * MAX_SHADOW_POINT_LIGHTS layers)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent.width = shadowMapSize;
    imageInfo.extent.height = shadowMapSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6 * MAX_SHADOW_POINT_LIGHTS;  // 6 faces per light
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &shadowCubeArrayImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow cube array image!");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, shadowCubeArrayImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = renderer->findMemoryType(memRequirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &shadowCubeArrayMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate point light shadow cube array memory!");
    }

    vkBindImageMemory(device, shadowCubeArrayImage, shadowCubeArrayMemory, 0);

    // Transition the image to SHADER_READ_ONLY_OPTIMAL layout to avoid validation errors
    // for layers that aren't written to by the shadow render pass
    VkCommandBuffer commandBuffer = renderer->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowCubeArrayImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6 * MAX_SHADOW_POINT_LIGHTS;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    renderer->endSingleTimeCommands(commandBuffer);

    // Create cube array view for shader sampling (as samplerCubeArray)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowCubeArrayImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6 * MAX_SHADOW_POINT_LIGHTS;

    if (vkCreateImageView(device, &viewInfo, nullptr, &shadowCubeArrayView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow cube array view!");
    }

    // Create per-face views for rendering (one 2D view per face per light)
    shadowCubeFaceViews.resize(6 * MAX_SHADOW_POINT_LIGHTS);
    for (int light = 0; light < MAX_SHADOW_POINT_LIGHTS; light++) {
        for (int face = 0; face < 6; face++) {
            VkImageViewCreateInfo faceViewInfo{};
            faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            faceViewInfo.image = shadowCubeArrayImage;
            faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            faceViewInfo.format = depthFormat;
            faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            faceViewInfo.subresourceRange.baseMipLevel = 0;
            faceViewInfo.subresourceRange.levelCount = 1;
            faceViewInfo.subresourceRange.baseArrayLayer = light * 6 + face;
            faceViewInfo.subresourceRange.layerCount = 1;

            int index = light * 6 + face;
            if (vkCreateImageView(device, &faceViewInfo, nullptr, &shadowCubeFaceViews[index]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create point light shadow face view!");
            }
        }
    }

    // Create sampler for shadow sampling
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
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow sampler!");
    }
}

void PointLightShadowSystem::createShadowRenderPass() {
    VkDevice device = renderer->getDevice();
    VkFormat depthFormat = renderer->findDepthFormat();

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Dependencies
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow render pass!");
    }
}

void PointLightShadowSystem::createShadowPipeline() {
    VkDevice device = renderer->getDevice();

    // Load shaders
    auto vertCode = renderer->readFile("shaders/shadow_point.vert.spv");
    auto fragCode = renderer->readFile("shaders/shadow_point.frag.spv");

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

    // Vertex input
    auto bindingDesc = Vertex::getBindingDescription();
    auto allAttrDescs = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> shadowAttrDescs;
    shadowAttrDescs.push_back(allAttrDescs[0]); // Position only

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(shadowAttrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = shadowAttrDescs.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport & scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Render both sides for omnidirectional
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    rasterizer.depthBiasClamp = 0.0f;

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
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blend (disabled)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    // Dynamic states
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix and face index
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(int);  // model matrix + face index

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow pipeline layout!");
    }

    // Create pipeline
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
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow pipeline!");
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

void PointLightShadowSystem::createShadowDescriptorResources() {
    VkDevice device = renderer->getDevice();
    uint32_t maxFrames = renderer->getMaxFramesInFlight();

    // Descriptor set layout - uniform buffer for light matrices
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &shadowDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create point light shadow descriptor set layout!");
    }

    // Create uniform buffers (Dynamic UBO size)
    VkDeviceSize bufferSize = dynamicAlignment * MAX_SHADOW_POINT_LIGHTS;
    shadowUniformBuffers.resize(maxFrames);
    shadowUniformBuffersMemory.resize(maxFrames);
    shadowUniformBuffersMapped.resize(maxFrames);

    for (size_t i = 0; i < maxFrames; i++) {
        renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            shadowUniformBuffers[i], shadowUniformBuffersMemory[i]);

        vkMapMemory(device, shadowUniformBuffersMemory[i], 0, bufferSize, 0, &shadowUniformBuffersMapped[i]);
    }

    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(maxFrames, shadowDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = renderer->getDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFrames);
    allocInfo.pSetLayouts = layouts.data();

    shadowDescriptorSets.resize(maxFrames);
    if (vkAllocateDescriptorSets(device, &allocInfo, shadowDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate point light shadow descriptor sets!");
    }

    // Update descriptor sets
    for (size_t i = 0; i < maxFrames; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShadowUniformBuffer); // Range for one descriptor is still the struct size

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = shadowDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
}

void PointLightShadowSystem::createShadowFramebuffers() {
    shadowFramebuffers.resize(6 * MAX_SHADOW_POINT_LIGHTS);

    for (int light = 0; light < MAX_SHADOW_POINT_LIGHTS; light++) {
        for (int face = 0; face < 6; face++) {
            int index = light * 6 + face;

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = shadowRenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &shadowCubeFaceViews[index];
            framebufferInfo.width = shadowMapSize;
            framebufferInfo.height = shadowMapSize;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(renderer->getDevice(), &framebufferInfo, nullptr, &shadowFramebuffers[index]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create point light shadow framebuffer!");
            }
        }
    }
}

std::array<glm::mat4, 6> PointLightShadowSystem::calculateCubeFaceMatrices(const glm::vec3& lightPos, float lightFarPlane) {
    std::array<glm::mat4, 6> matrices;

    // Perspective projection for 90 degree FOV (cube face)
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, lightFarPlane);

    // Vulkan depth correction [0,1] range
    glm::mat4 depthCorrection = glm::mat4(1.0f);
    depthCorrection[2][2] = 0.5f;
    depthCorrection[3][2] = 0.5f;
    projection = depthCorrection * projection;

    // View matrices for each cube face
    // +X (right)
    matrices[0] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    // -X (left)
    matrices[1] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    // +Y (up)
    matrices[2] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    // -Y (down)
    matrices[3] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    // +Z (front)
    matrices[4] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    // -Z (back)
    matrices[5] = projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    return matrices;
}

void PointLightShadowSystem::updateLightMatrices(const std::vector<Scene::Light>& lights, uint32_t frameIndex) {
    activeShadowLightCount = 0;

    // Find point lights (non-directional with position.w = 1)
    for (const auto& light : lights) {
        if (!light.isDirectional && activeShadowLightCount < MAX_SHADOW_POINT_LIGHTS) {
            // Use light radius as far plane, or default if radius is 0
            float lightFarPlane = light.radius > 0.0f ? light.radius : farPlane;

            shadowLightInfo[activeShadowLightCount].position = glm::vec4(light.position, lightFarPlane);
            activeShadowLightCount++;
        }
    }
}

void PointLightShadowSystem::updateShadowUniformBuffer(uint32_t frameIndex, int lightIndex, const glm::vec3& lightPos) {
    float lightFarPlane = shadowLightInfo[lightIndex].position.w;
    auto matrices = calculateCubeFaceMatrices(lightPos, lightFarPlane);

    ShadowUniformBuffer ubo{};
    for (int i = 0; i < 6; i++) {
        ubo.lightViewProj[i] = matrices[i];
    }
    ubo.lightPos = glm::vec4(lightPos, lightFarPlane);

    // Write to the correct offset in the dynamic buffer
    char* mappedData = static_cast<char*>(shadowUniformBuffersMapped[frameIndex]);
    memcpy(mappedData + (lightIndex * dynamicAlignment), &ubo, sizeof(ubo));
}

void PointLightShadowSystem::renderShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances, uint32_t frameIndex) {
    if (!enabled || activeShadowLightCount == 0) return;

    // Render shadow maps for each active point light
    for (int lightIndex = 0; lightIndex < activeShadowLightCount; lightIndex++) {
        glm::vec3 lightPos = glm::vec3(shadowLightInfo[lightIndex].position);

        // Update uniform buffer for this light
        updateShadowUniformBuffer(frameIndex, lightIndex, lightPos);

        // Render all 6 faces
        renderLightShadowPass(commandBuffer, instances, frameIndex, lightIndex);
    }
}

void PointLightShadowSystem::renderLightShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances,
                                                    uint32_t frameIndex, int lightIndex) {
    // Render each face of the cubemap
    for (int face = 0; face < 6; face++) {
        int framebufferIndex = lightIndex * 6 + face;

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = shadowRenderPass;
        renderPassInfo.framebuffer = shadowFramebuffers[framebufferIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {shadowMapSize, shadowMapSize};

        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

        // Set viewport and scissor
        VkViewport viewport{};
        viewport.width = static_cast<float>(shadowMapSize);
        viewport.height = static_cast<float>(shadowMapSize);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = {shadowMapSize, shadowMapSize};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Bind descriptor set with dynamic offset
        uint32_t dynamicOffset = static_cast<uint32_t>(lightIndex * dynamicAlignment);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            shadowPipelineLayout, 0, 1, &shadowDescriptorSets[frameIndex], 1, &dynamicOffset);

        // Render all instances
        for (const auto& instance : instances) {
            if (!instance.mesh) continue;

            glm::mat4 modelMatrix = instance.transform.getModelMatrix();

            // Push constants: model matrix + face index
            struct PushData {
                glm::mat4 model;
                int faceIndex;
            } pushData;
            pushData.model = modelMatrix;
            pushData.faceIndex = face;

            vkCmdPushConstants(commandBuffer, shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushData), &pushData);

            // Draw mesh
            instance.mesh->bind(commandBuffer);
            vkCmdDrawIndexed(commandBuffer, instance.mesh->indexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);
    }
}

// Configuration methods
void PointLightShadowSystem::setResolution(uint32_t size) {
    shadowMapSize = size;
}

void PointLightShadowSystem::setBias(float constantFactor, float slopeFactor) {
    depthBiasConstant = constantFactor;
    depthBiasSlopeFactor = slopeFactor;
}

void PointLightShadowSystem::setNearFarPlanes(float near, float far) {
    nearPlane = near;
    farPlane = far;
}
