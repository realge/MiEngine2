#include "include/culling/FrustumCulling.h"
#include "VulkanRenderer.h"
#include <iostream>
#include <algorithm>

namespace MiEngine {

// ============================================================================
// Frustum Implementation
// ============================================================================

void Frustum::extractFromViewProj(const glm::mat4& vp) {
    // Extract frustum planes from view-projection matrix
    // Method: Gribb/Hartmann "Fast Extraction of Viewing Frustum Planes"

    // Left plane
    planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );

    // Right plane
    planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );

    // Bottom plane
    planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );

    // Top plane
    planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );

    // Near plane
    planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );

    // Far plane
    planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize all planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0001f) {
            planes[i] /= len;
        }
    }
}

bool Frustum::testSphere(const glm::vec3& center, float radius) const {
    for (int i = 0; i < 6; i++) {
        float distance = glm::dot(glm::vec3(planes[i]), center) + planes[i].w;
        if (distance < -radius) {
            return false;  // Completely outside this plane
        }
    }
    return true;  // Inside or intersecting all planes
}

bool Frustum::testAABB(const glm::vec3& min, const glm::vec3& max) const {
    for (int i = 0; i < 6; i++) {
        glm::vec3 normal = glm::vec3(planes[i]);

        // Find the corner that is most positive along the plane normal
        glm::vec3 pVertex;
        pVertex.x = (normal.x >= 0) ? max.x : min.x;
        pVertex.y = (normal.y >= 0) ? max.y : min.y;
        pVertex.z = (normal.z >= 0) ? max.z : min.z;

        // If the most positive corner is outside, the AABB is outside
        if (glm::dot(normal, pVertex) + planes[i].w < 0) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// FrustumCulling Implementation
// ============================================================================

FrustumCulling::FrustumCulling() {}

FrustumCulling::~FrustumCulling() {
    cleanup();
}

bool FrustumCulling::initialize(VulkanRenderer* renderer) {
    m_Renderer = renderer;

    if (!m_Renderer) {
        std::cerr << "FrustumCulling: Invalid renderer" << std::endl;
        return false;
    }

    // Create GPU resources for compute-based culling
    try {
        createDescriptorSetLayout();
        createUniformBuffer();
        createComputePipeline();
        m_GPUCullingReady = true;
        std::cout << "FrustumCulling: GPU culling initialized" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "FrustumCulling: Failed to initialize GPU culling: " << e.what() << std::endl;
        std::cerr << "FrustumCulling: Falling back to CPU culling" << std::endl;
        m_GPUCullingReady = false;
    }

    return true;
}

void FrustumCulling::cleanup() {
    if (!m_Renderer) return;

    VkDevice device = m_Renderer->getDevice();
    vkDeviceWaitIdle(device);

    // Cleanup compute pipeline
    if (m_ComputePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_ComputePipeline, nullptr);
        m_ComputePipeline = VK_NULL_HANDLE;
    }
    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup uniform buffers
    for (size_t i = 0; i < m_UniformBuffers.size(); i++) {
        if (m_UniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_UniformBuffers[i], nullptr);
        }
        if (m_UniformMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_UniformMemory[i], nullptr);
        }
    }
    m_UniformBuffers.clear();
    m_UniformMemory.clear();
    m_UniformMapped.clear();

    m_GPUCullingReady = false;
}

void FrustumCulling::cullCPU(const Frustum& frustum,
                              const std::vector<BoundingVolume>& bounds,
                              std::vector<uint32_t>& visibleIndices) {
    visibleIndices.clear();
    visibleIndices.reserve(bounds.size());

    for (uint32_t i = 0; i < bounds.size(); i++) {
        const auto& bv = bounds[i];

        // Distance culling (optional)
        if (m_EnableDistanceCull) {
            float distance = glm::length(bv.sphereCenter - m_CameraPosition);
            if (distance - bv.sphereRadius > m_MaxDrawDistance) {
                continue;
            }
        }

        // Frustum culling (use sphere for speed, AABB for accuracy if needed)
        if (m_EnableFrustumCull) {
            if (!frustum.testSphere(bv.sphereCenter, bv.sphereRadius)) {
                continue;
            }
        }

        visibleIndices.push_back(i);
    }
}

void FrustumCulling::cullGPU(VkCommandBuffer cmd,
                              uint32_t objectCount,
                              VkBuffer inputBuffer,
                              VkBuffer outputBuffer,
                              VkBuffer countBuffer) {
    if (!m_GPUCullingReady || objectCount == 0) {
        return;
    }

    // Update uniforms for this frame
    updateUniforms();

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_PipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

    // Reset visible count to 0
    vkCmdFillBuffer(cmd, countBuffer, 0, sizeof(uint32_t), 0);

    // Memory barrier for count buffer
    VkMemoryBarrier countBarrier{};
    countBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    countBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    countBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &countBarrier, 0, nullptr, 0, nullptr);

    // Dispatch compute shader (64 objects per workgroup)
    uint32_t workgroupCount = (objectCount + 63) / 64;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier for output buffer
    VkMemoryBarrier outputBarrier{};
    outputBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    outputBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &outputBarrier, 0, nullptr, 0, nullptr);
}

void FrustumCulling::updateFrustum(const glm::mat4& view, const glm::mat4& proj) {
    m_ViewProj = proj * view;
    m_Frustum.extractFromViewProj(m_ViewProj);

    // Extract camera position from inverse view matrix
    glm::mat4 invView = glm::inverse(view);
    m_CameraPosition = glm::vec3(invView[3]);
}

void FrustumCulling::createDescriptorSetLayout() {
    VkDevice device = m_Renderer->getDevice();

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Binding 0: Uniform buffer (frustum planes, settings)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Input storage buffer (CullInputData[])
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Output storage buffer (visible indices + count)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create frustum culling descriptor set layout");
    }
}

void FrustumCulling::createUniformBuffer() {
    VkDevice device = m_Renderer->getDevice();
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();

    VkDeviceSize bufferSize = sizeof(CullUniforms);

    m_UniformBuffers.resize(framesInFlight);
    m_UniformMemory.resize(framesInFlight);
    m_UniformMapped.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_Renderer->createBuffer(bufferSize,
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 m_UniformBuffers[i],
                                 m_UniformMemory[i]);

        vkMapMemory(device, m_UniformMemory[i], 0, bufferSize, 0, &m_UniformMapped[i]);
    }
}

void FrustumCulling::createComputePipeline() {
    VkDevice device = m_Renderer->getDevice();

    // Load compute shader
    auto shaderCode = m_Renderer->readFile("shaders/culling/frustum_cull.comp.spv");

    VkShaderModule shaderModule = m_Renderer->createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        throw std::runtime_error("Failed to create frustum culling pipeline layout");
    }

    // Compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_PipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_ComputePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        throw std::runtime_error("Failed to create frustum culling compute pipeline");
    }

    vkDestroyShaderModule(device, shaderModule, nullptr);
}

void FrustumCulling::updateUniforms() {
    // This would be called per-frame with the current frame index
    // For now, update the first buffer
    CullUniforms uniforms{};
    uniforms.viewProj = m_ViewProj;
    for (int i = 0; i < 6; i++) {
        uniforms.frustumPlanes[i] = m_Frustum.planes[i];
    }
    uniforms.cameraPosition = glm::vec4(m_CameraPosition, 1.0f);
    uniforms.objectCount = 0;  // Will be set per-dispatch
    uniforms.enableFrustumCull = m_EnableFrustumCull ? 1 : 0;
    uniforms.enableDistanceCull = m_EnableDistanceCull ? 1 : 0;
    uniforms.maxDrawDistance = m_MaxDrawDistance;

    if (!m_UniformMapped.empty() && m_UniformMapped[0]) {
        memcpy(m_UniformMapped[0], &uniforms, sizeof(uniforms));
    }
}

} // namespace MiEngine
