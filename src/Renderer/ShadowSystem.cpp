#include "Renderer/ShadowSystem.h"
#include "VulkanRenderer.h"
#include "scene/Scene.h"
#include "Utils/CommonVertex.h"
#include "Utils/SkeletalVertex.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <iostream>
#include <array>

ShadowSystem::ShadowSystem(VulkanRenderer* renderer)
    : renderer(renderer), lightSpaceMatrix(glm::mat4(1.0f)) {
}

ShadowSystem::~ShadowSystem() {
    cleanup();
}

void ShadowSystem::initialize() {
    std::cout << "Initializing Shadow System..." << std::endl;

    createShadowResources();
    createShadowRenderPass();
    createShadowDescriptorResources();
    createShadowPipeline();
    createSkeletalShadowPipeline();
    createShadowFramebuffer();

    std::cout << "Shadow System initialized successfully" << std::endl;
}

void ShadowSystem::cleanup() {
    VkDevice device = renderer->getDevice();

    // Cleanup skeletal shadow pipeline
    if (skeletalShadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skeletalShadowPipeline, nullptr);
        skeletalShadowPipeline = VK_NULL_HANDLE;
    }

    if (skeletalShadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skeletalShadowPipelineLayout, nullptr);
        skeletalShadowPipelineLayout = VK_NULL_HANDLE;
    }

    if (skeletalShadowDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skeletalShadowDescriptorSetLayout, nullptr);
        skeletalShadowDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup static shadow pipeline
    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, shadowPipeline, nullptr);
        shadowPipeline = VK_NULL_HANDLE;
    }

    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        shadowPipelineLayout = VK_NULL_HANDLE;
    }
    
    // Cleanup framebuffer
    if (shadowFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
        shadowFramebuffer = VK_NULL_HANDLE;
    }
    
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
    
    // Cleanup image resources
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }
    
    if (shadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowImageView, nullptr);
        shadowImageView = VK_NULL_HANDLE;
    }
    
    if (shadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, shadowImage, nullptr);
        shadowImage = VK_NULL_HANDLE;
    }
    
    if (shadowImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, shadowImageMemory, nullptr);
        shadowImageMemory = VK_NULL_HANDLE;
    }
}

void ShadowSystem::createShadowResources() {
    // 1. Create Depth Image
    VkFormat depthFormat = renderer->findDepthFormat();
    
    renderer->createImage(shadowMapWidth, shadowMapHeight, depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        shadowImage, shadowImageMemory);

    // 2. Create Image View
    shadowImageView = renderer->createImageView(shadowImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // 3. Create Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(renderer->getDevice(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }
}

void ShadowSystem::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = renderer->findDepthFormat();
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

    // Dependencies to handle layout transitions
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

    if (vkCreateRenderPass(renderer->getDevice(), &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
}

void ShadowSystem::createShadowPipeline() {
    // 1. Load Shadow Vertex Shader
    auto vertCode = renderer->readFile("shaders/shadow.vert.spv");
    VkShaderModule vertModule = renderer->createShaderModule(vertCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage };

    // 2. Vertex Input State
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

    // 3. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 4. Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapWidth);
    viewport.height = static_cast<float>(shadowMapHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapWidth, shadowMapHeight};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 5. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    rasterizer.depthBiasClamp = 0.0f;

    // 6. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 7. Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 8. Color Blend (Disabled)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    // 9. Dynamic States
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // 10. Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(renderer->getDevice(), &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline layout!");
    }

    // 11. Create Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
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

    if (vkCreateGraphicsPipelines(renderer->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline!");
    }

    vkDestroyShaderModule(renderer->getDevice(), vertModule, nullptr);
}

void ShadowSystem::createSkeletalShadowPipeline() {
    // 1. Load Skeletal Shadow Vertex Shader
    auto vertCode = renderer->readFile("shaders/shadow_skeletal.vert.spv");
    VkShaderModule vertModule = renderer->createShaderModule(vertCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage };

    // 2. Vertex Input State - use SkeletalVertex
    auto bindingDesc = MiEngine::SkeletalVertex::getBindingDescription();
    auto attrDescs = MiEngine::SkeletalVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 4. Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapWidth);
    viewport.height = static_cast<float>(shadowMapHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapWidth, shadowMapHeight};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 5. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    rasterizer.depthBiasClamp = 0.0f;

    // 6. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 7. Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 8. Color Blend (Disabled)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    // 9. Dynamic States
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // 10. Create descriptor set layout for skeletal shadow (set 0 = light matrix, set 1 = bone matrices)
    // We reuse the shadowDescriptorSetLayout for set 0
    // We need to get the bone matrix descriptor set layout from the renderer
    VkDescriptorSetLayout boneMatrixLayout = renderer->getBoneMatrixDescriptorSetLayout();

    std::array<VkDescriptorSetLayout, 2> setLayouts = {
        shadowDescriptorSetLayout,  // Set 0: light space matrix
        boneMatrixLayout            // Set 1: bone matrices
    };

    // 11. Pipeline Layout with both descriptor sets
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(renderer->getDevice(), &pipelineLayoutInfo, nullptr, &skeletalShadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skeletal shadow pipeline layout!");
    }

    // 12. Create Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = skeletalShadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(renderer->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skeletalShadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skeletal shadow pipeline!");
    }

    vkDestroyShaderModule(renderer->getDevice(), vertModule, nullptr);
}

void ShadowSystem::createShadowDescriptorResources() {
    VkDevice device = renderer->getDevice();
    uint32_t maxFrames = renderer->getMaxFramesInFlight();
    
    // 1. Create Descriptor Set Layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &shadowDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow descriptor set layout!");
    }

    // 2. Create Uniform Buffers
    VkDeviceSize bufferSize = sizeof(ShadowUniformBuffer);
    shadowUniformBuffers.resize(maxFrames);
    shadowUniformBuffersMemory.resize(maxFrames);
    shadowUniformBuffersMapped.resize(maxFrames);

    for (size_t i = 0; i < maxFrames; i++) {
        renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            shadowUniformBuffers[i], shadowUniformBuffersMemory[i]);
        
        vkMapMemory(device, shadowUniformBuffersMemory[i], 0, bufferSize, 0, &shadowUniformBuffersMapped[i]);
    }

    // 3. Allocate Descriptor Sets
    std::vector<VkDescriptorSetLayout> layouts(maxFrames, shadowDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = renderer->getDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFrames);
    allocInfo.pSetLayouts = layouts.data();

    shadowDescriptorSets.resize(maxFrames);
    if (vkAllocateDescriptorSets(device, &allocInfo, shadowDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate shadow descriptor sets!");
    }

    // 4. Update Descriptor Sets
    for (size_t i = 0; i < maxFrames; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowUniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShadowUniformBuffer);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = shadowDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
}

void ShadowSystem::createShadowFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowImageView;
    framebufferInfo.width = shadowMapWidth;
    framebufferInfo.height = shadowMapHeight;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(renderer->getDevice(), &framebufferInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

void ShadowSystem::updateLightMatrix(const std::vector<Scene::Light>& lights, uint32_t frameIndex, const glm::vec3& cameraPosition) {
    // Find first directional light
    glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f); // Default direction
    
    for (const auto& light : lights) {
        if (light.isDirectional) {
            lightDir = glm::normalize(light.position);
            break;
        }
    }
    
    // Calculate light space matrix
    lightSpaceMatrix = calculateLightSpaceMatrix(lightDir, cameraPosition);
    
    // Update uniform buffer
    updateShadowUniformBuffer(frameIndex);
}

void ShadowSystem::renderShadowPass(VkCommandBuffer commandBuffer, const std::vector<MeshInstance>& instances, uint32_t frameIndex) {
    if (!enabled) return;

    VkRenderPassBeginInfo shadowRPInfo{};
    shadowRPInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    shadowRPInfo.renderPass = shadowRenderPass;
    shadowRPInfo.framebuffer = shadowFramebuffer;
    shadowRPInfo.renderArea.offset = {0, 0};
    shadowRPInfo.renderArea.extent = {shadowMapWidth, shadowMapHeight};

    VkClearValue clearValues[1];
    clearValues[0].depthStencil = {1.0f, 0};
    shadowRPInfo.clearValueCount = 1;
    shadowRPInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &shadowRPInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set Shadow Viewport and Scissor (shared by both pipelines)
    VkViewport shadowViewport{};
    shadowViewport.width = static_cast<float>(shadowMapWidth);
    shadowViewport.height = static_cast<float>(shadowMapHeight);
    shadowViewport.minDepth = 0.0f;
    shadowViewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &shadowViewport);

    VkRect2D shadowScissor{};
    shadowScissor.extent = {shadowMapWidth, shadowMapHeight};
    vkCmdSetScissor(commandBuffer, 0, 1, &shadowScissor);

    // Track which pipeline is currently bound
    enum class BoundPipeline { None, Static, Skeletal };
    BoundPipeline currentPipeline = BoundPipeline::None;

    // Render Scene Geometry (Depth Only)
    for (const auto& instance : instances) {
        if (!instance.mesh) continue;

        // Get the Model Matrix from the Transform
        glm::mat4 modelMatrix = instance.transform.getModelMatrix();

        // Check if this is a skeletal mesh
        if (instance.isSkeletal && instance.skeletalMesh && skeletalShadowPipeline != VK_NULL_HANDLE) {
            // Use skeletal shadow pipeline
            if (currentPipeline != BoundPipeline::Skeletal) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skeletalShadowPipeline);
                currentPipeline = BoundPipeline::Skeletal;
            }

            // Ensure skeletal instance resources are created (Shadow pass runs before main pass)
            renderer->createSkeletalInstanceResources(instance.instanceId, instance.skeletalMesh->getBoneCount());
            
            // Update bone matrices for this frame
            const auto& boneMatrices = instance.skeletalMesh->getFinalBoneMatrices();
            renderer->updateBoneMatrices(instance.instanceId, boneMatrices, frameIndex);

            // Bind shadow descriptor set (set 0)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                skeletalShadowPipelineLayout, 0, 1, &shadowDescriptorSets[frameIndex], 0, nullptr);

            // Bind bone matrix descriptor set (set 1)
            VkDescriptorSet boneDescriptorSet = renderer->getBoneMatrixDescriptorSet(instance.instanceId, frameIndex);
            if (boneDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    skeletalShadowPipelineLayout, 1, 1, &boneDescriptorSet, 0, nullptr);
            }

            // Push Model Matrix
            vkCmdPushConstants(commandBuffer, skeletalShadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);

            // Draw Mesh
            instance.mesh->bind(commandBuffer);
            uint32_t count = instance.mesh->indexCount;
            vkCmdDrawIndexed(commandBuffer, count, 1, 0, 0, 0);
        } else {
            // Use static shadow pipeline
            if (currentPipeline != BoundPipeline::Static) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

                // Bind Shadow Descriptor Set
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shadowPipelineLayout, 0, 1, &shadowDescriptorSets[frameIndex], 0, nullptr);

                currentPipeline = BoundPipeline::Static;
            }

            // Push Model Matrix
            vkCmdPushConstants(commandBuffer, shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);

            // Draw Mesh
            instance.mesh->bind(commandBuffer);
            uint32_t count = instance.mesh->indexCount;
            vkCmdDrawIndexed(commandBuffer, count, 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);
}

void ShadowSystem::updateShadowUniformBuffer(uint32_t frameIndex) {
    ShadowUniformBuffer ubo{};
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    memcpy(shadowUniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

glm::mat4 ShadowSystem::calculateLightSpaceMatrix(const glm::vec3& lightDirection, const glm::vec3& cameraPosition) {
    // Orthographic projection for directional light
    glm::mat4 lightProjection = glm::ortho(-frustumSize, frustumSize, -frustumSize, frustumSize, nearPlane, farPlane);
    
    // Adjust for Vulkan's [0,1] depth range
    glm::mat4 depthCorrection = glm::mat4(1.0f);
    depthCorrection[2][2] = 0.5f;
    depthCorrection[3][2] = 0.5f;
    lightProjection = depthCorrection * lightProjection;

    // Calculate view matrix centered on camera
    // We want the light camera to follow the player but snap to texel units to avoid shimmering
    
    // 1. Calculate basic light view matrix centered on origin
    glm::vec3 upVector = glm::vec3(0.0f, 1.0f, 0.0f);
    if (abs(glm::dot(glm::normalize(lightDirection), upVector)) > 0.99f) {
        upVector = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    
    // Position light camera high above the player along the light direction
    // We use the camera position as the focus point
    glm::vec3 lightPos = cameraPosition - glm::normalize(lightDirection) * (farPlane * 0.5f);
    
    glm::mat4 lightView = glm::lookAt(lightPos, cameraPosition, upVector);
    
    // 2. Texel snapping
    // Transform camera position to light space
    glm::mat4 shadowMatrix = lightProjection * lightView;
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin = shadowOrigin * (static_cast<float>(shadowMapWidth) / 2.0f);
    
    glm::vec4 roundedOrigin = glm::round(shadowOrigin);
    glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
    roundOffset = roundOffset * (2.0f / static_cast<float>(shadowMapWidth));
    roundOffset.z = 0.0f;
    roundOffset.w = 0.0f;
    
    // Apply offset to projection matrix
    lightProjection[3] += roundOffset;
    
    return lightProjection * lightView;
}

// Configuration methods
void ShadowSystem::setResolution(uint32_t width, uint32_t height) {
    shadowMapWidth = width;
    shadowMapHeight = height;
}

void ShadowSystem::setBias(float constantFactor, float slopeFactor) {
    depthBiasConstant = constantFactor;
    depthBiasSlopeFactor = slopeFactor;
}

void ShadowSystem::setFrustumSize(float size) {
    frustumSize = size;
}

void ShadowSystem::setDepthRange(float near, float far) {
    nearPlane = near;
    farPlane = far;
}
