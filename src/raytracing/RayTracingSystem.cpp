#include "../../include/raytracing/RayTracingSystem.h"
#include "../../VulkanRenderer.h"
#include "../../include/scene/Scene.h"
#include "../../include/Renderer/IBLSystem.h"
#include "../../include/mesh/Mesh.h"
#include "../../include/core/MiWorld.h"
#include "../../include/core/MiActor.h"
#include "../../include/component/MiStaticMeshComponent.h"

#include <iostream>
#include <fstream>
#include <cstring>

namespace MiEngine {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RayTracingSystem::RayTracingSystem(VulkanRenderer* renderer)
    : m_Renderer(renderer)
{
}

RayTracingSystem::~RayTracingSystem() {
    cleanup();
}

// ============================================================================
// Static Support Check
// ============================================================================

RTFeatureSupport RayTracingSystem::checkSupport(VkPhysicalDevice physicalDevice) {
    RTFeatureSupport support;

    // Check for required extensions
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    auto hasExtension = [&](const char* name) {
        for (const auto& ext : availableExtensions) {
            if (strcmp(ext.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    support.accelerationStructure = hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    support.rayTracingPipeline = hasExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    support.bufferDeviceAddress = hasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // Check for optional ray query extension
    support.rayQuery = hasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);

    // All required extensions must be present
    if (!support.accelerationStructure) {
        support.unsupportedReason = "Missing VK_KHR_acceleration_structure extension";
    } else if (!support.rayTracingPipeline) {
        support.unsupportedReason = "Missing VK_KHR_ray_tracing_pipeline extension";
    } else if (!support.bufferDeviceAddress) {
        support.unsupportedReason = "Missing VK_KHR_buffer_device_address extension";
    } else {
        support.supported = true;
    }

    return support;
}

// ============================================================================
// Initialization
// ============================================================================

bool RayTracingSystem::initialize() {
    if (m_Initialized) {
        return true;
    }

    // Check device support
    m_FeatureSupport = checkSupport(m_Renderer->getPhysicalDevice());
    if (!m_FeatureSupport.supported) {
        std::cerr << "Ray tracing not supported: " << m_FeatureSupport.unsupportedReason << std::endl;
        return false;
    }

    std::cout << "RT Features:" << std::endl;
    std::cout << "  - Acceleration Structure: " << (m_FeatureSupport.accelerationStructure ? "Yes" : "No") << std::endl;
    std::cout << "  - Ray Tracing Pipeline: " << (m_FeatureSupport.rayTracingPipeline ? "Yes" : "No") << std::endl;
    std::cout << "  - Ray Query: " << (m_FeatureSupport.rayQuery ? "Yes" : "No") << std::endl;
    std::cout << "  - Buffer Device Address: " << (m_FeatureSupport.bufferDeviceAddress ? "Yes" : "No") << std::endl;

    // Load extension functions
    loadExtensionFunctions();

    // Query RT properties
    queryRTProperties();

    std::cout << "RT Pipeline Properties:" << std::endl;
    std::cout << "  - Shader Group Handle Size: " << m_PipelineProps.shaderGroupHandleSize << std::endl;
    std::cout << "  - Shader Group Handle Alignment: " << m_PipelineProps.shaderGroupHandleAlignment << std::endl;
    std::cout << "  - Shader Group Base Alignment: " << m_PipelineProps.shaderGroupBaseAlignment << std::endl;
    std::cout << "  - Max Ray Recursion Depth: " << m_PipelineProps.maxRayRecursionDepth << std::endl;

    std::cout << "RT Acceleration Structure Properties:" << std::endl;
    std::cout << "  - Max Geometry Count: " << m_ASProps.maxGeometryCount << std::endl;
    std::cout << "  - Max Instance Count: " << m_ASProps.maxInstanceCount << std::endl;
    std::cout << "  - Max Primitive Count: " << m_ASProps.maxPrimitiveCount << std::endl;

    // Store output dimensions
    m_OutputWidth = m_Renderer->getSwapChainExtent().width;
    m_OutputHeight = m_Renderer->getSwapChainExtent().height;

    // Create RT resources
    if (!createDescriptorSetLayout()) {
        std::cerr << "Failed to create RT descriptor set layout" << std::endl;
        return false;
    }

    if (!createOutputImages()) {
        std::cerr << "Failed to create RT output images" << std::endl;
        return false;
    }

    if (!createUniformBuffers()) {
        std::cerr << "Failed to create RT uniform buffers" << std::endl;
        return false;
    }

    if (!createMaterialBuffer()) {
        std::cerr << "Failed to create RT material buffer" << std::endl;
        return false;
    }

    if (!createGeometryBuffers()) {
        std::cerr << "Failed to create RT geometry buffers" << std::endl;
        return false;
    }

    if (!createDescriptorPool()) {
        std::cerr << "Failed to create RT descriptor pool" << std::endl;
        return false;
    }

    if (!createDescriptorSets()) {
        std::cerr << "Failed to create RT descriptor sets" << std::endl;
        return false;
    }

    if (!createRTPipeline()) {
        std::cerr << "Failed to create RT pipeline" << std::endl;
        return false;
    }

    if (!createShaderBindingTable()) {
        std::cerr << "Failed to create shader binding table" << std::endl;
        return false;
    }

    // Initialize denoiser resources
    if (!createHistoryBuffers()) {
        std::cerr << "Failed to create denoiser history buffers" << std::endl;
        return false;
    }

    if (!createDenoisePipelines()) {
        std::cerr << "Failed to create denoise pipelines" << std::endl;
        return false;
    }

    if (!createDenoiseDescriptorSets()) {
        std::cerr << "Failed to create denoise descriptor sets" << std::endl;
        return false;
    }

    m_Initialized = true;
    std::cout << "Ray Tracing System initialized successfully" << std::endl;

    return true;
}

void RayTracingSystem::cleanup() {
    if (!m_Initialized) {
        return;
    }

    VkDevice device = m_Renderer->getDevice();
    vkDeviceWaitIdle(device);

    // Cleanup denoiser resources first
    cleanupDenoiser();

    // Cleanup uniform buffers
    for (size_t i = 0; i < m_UniformBuffers.size(); i++) {
        if (m_UniformBuffersMapped[i]) {
            vkUnmapMemory(device, m_UniformBuffersMemory[i]);
        }
        if (m_UniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_UniformBuffers[i], nullptr);
        }
        if (m_UniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_UniformBuffersMemory[i], nullptr);
        }
    }
    m_UniformBuffers.clear();
    m_UniformBuffersMemory.clear();
    m_UniformBuffersMapped.clear();

    // Cleanup output images
    if (m_ReflectionImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_ReflectionImageView, nullptr);
        m_ReflectionImageView = VK_NULL_HANDLE;
    }
    if (m_ReflectionImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_ReflectionImage, nullptr);
        m_ReflectionImage = VK_NULL_HANDLE;
    }
    if (m_ReflectionMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_ReflectionMemory, nullptr);
        m_ReflectionMemory = VK_NULL_HANDLE;
    }

    if (m_ShadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_ShadowImageView, nullptr);
        m_ShadowImageView = VK_NULL_HANDLE;
    }
    if (m_ShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_ShadowImage, nullptr);
        m_ShadowImage = VK_NULL_HANDLE;
    }
    if (m_ShadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_ShadowMemory, nullptr);
        m_ShadowMemory = VK_NULL_HANDLE;
    }

    if (m_OutputSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_OutputSampler, nullptr);
        m_OutputSampler = VK_NULL_HANDLE;
    }

    // Cleanup SBT
    if (m_SBTBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_SBTBuffer, nullptr);
        m_SBTBuffer = VK_NULL_HANDLE;
    }
    if (m_SBTMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_SBTMemory, nullptr);
        m_SBTMemory = VK_NULL_HANDLE;
    }

    // Cleanup RT pipeline
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    // Cleanup descriptors
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_OutputDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_OutputDescriptorSetLayout, nullptr);
        m_OutputDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup TLAS
    if (m_TLAS.handle != VK_NULL_HANDLE && pfnDestroyAccelerationStructureKHR) {
        pfnDestroyAccelerationStructureKHR(device, m_TLAS.handle, nullptr);
        m_TLAS.handle = VK_NULL_HANDLE;
    }
    if (m_TLAS.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_TLAS.buffer, nullptr);
        m_TLAS.buffer = VK_NULL_HANDLE;
    }
    if (m_TLAS.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_TLAS.memory, nullptr);
        m_TLAS.memory = VK_NULL_HANDLE;
    }
    if (m_TLAS.instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_TLAS.instanceBuffer, nullptr);
        m_TLAS.instanceBuffer = VK_NULL_HANDLE;
    }
    if (m_TLAS.instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_TLAS.instanceMemory, nullptr);
        m_TLAS.instanceMemory = VK_NULL_HANDLE;
    }

    // Cleanup BLAS
    for (auto& [id, blas] : m_BLASMap) {
        if (blas.handle != VK_NULL_HANDLE && pfnDestroyAccelerationStructureKHR) {
            pfnDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
        }
        if (blas.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, blas.buffer, nullptr);
        }
        if (blas.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, blas.memory, nullptr);
        }
        // Cleanup RT geometry buffers
        if (blas.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, blas.vertexBuffer, nullptr);
        }
        if (blas.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, blas.vertexMemory, nullptr);
        }
        if (blas.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, blas.indexBuffer, nullptr);
        }
        if (blas.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, blas.indexMemory, nullptr);
        }
    }
    m_BLASMap.clear();

    // Cleanup scratch buffer
    if (m_ScratchBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_ScratchBuffer, nullptr);
        m_ScratchBuffer = VK_NULL_HANDLE;
    }
    if (m_ScratchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_ScratchMemory, nullptr);
        m_ScratchMemory = VK_NULL_HANDLE;
    }

    // Cleanup geometry buffers
    if (m_GeometryVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_GeometryVertexBuffer, nullptr);
        m_GeometryVertexBuffer = VK_NULL_HANDLE;
    }
    if (m_GeometryVertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_GeometryVertexMemory, nullptr);
        m_GeometryVertexMemory = VK_NULL_HANDLE;
    }
    if (m_GeometryIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_GeometryIndexBuffer, nullptr);
        m_GeometryIndexBuffer = VK_NULL_HANDLE;
    }
    if (m_GeometryIndexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_GeometryIndexMemory, nullptr);
        m_GeometryIndexMemory = VK_NULL_HANDLE;
    }
    if (m_MeshInfoBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_MeshInfoBuffer, nullptr);
        m_MeshInfoBuffer = VK_NULL_HANDLE;
    }
    if (m_MeshInfoMemory != VK_NULL_HANDLE) {
        if (m_MeshInfoMapped) {
            vkUnmapMemory(device, m_MeshInfoMemory);
            m_MeshInfoMapped = nullptr;
        }
        vkFreeMemory(device, m_MeshInfoMemory, nullptr);
        m_MeshInfoMemory = VK_NULL_HANDLE;
    }
    if (m_InstanceDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_InstanceDataBuffer, nullptr);
        m_InstanceDataBuffer = VK_NULL_HANDLE;
    }
    if (m_InstanceDataMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_InstanceDataMemory, nullptr);
        m_InstanceDataMemory = VK_NULL_HANDLE;
    }
    if (m_MaterialBufferMapped) {
        vkUnmapMemory(device, m_MaterialMemory);
        m_MaterialBufferMapped = nullptr;
    }
    if (m_MaterialBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_MaterialBuffer, nullptr);
        m_MaterialBuffer = VK_NULL_HANDLE;
    }
    if (m_MaterialMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_MaterialMemory, nullptr);
        m_MaterialMemory = VK_NULL_HANDLE;
    }

    m_Initialized = false;
}

// ============================================================================
// Extension Function Loading
// ============================================================================

void RayTracingSystem::loadExtensionFunctions() {
    VkDevice device = m_Renderer->getDevice();

    // Acceleration structure functions
    pfnGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));

    pfnCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));

    pfnDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));

    pfnCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));

    pfnGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));

    // Ray tracing pipeline functions
    pfnCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
        vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));

    pfnGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
        vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));

    pfnCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
        vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));

    // Buffer device address
    pfnGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR"));

    // Verify all required functions are loaded
    if (!pfnGetAccelerationStructureBuildSizesKHR ||
        !pfnCreateAccelerationStructureKHR ||
        !pfnDestroyAccelerationStructureKHR ||
        !pfnCmdBuildAccelerationStructuresKHR ||
        !pfnGetAccelerationStructureDeviceAddressKHR ||
        !pfnCreateRayTracingPipelinesKHR ||
        !pfnGetRayTracingShaderGroupHandlesKHR ||
        !pfnCmdTraceRaysKHR ||
        !pfnGetBufferDeviceAddressKHR) {
        throw std::runtime_error("Failed to load required ray tracing extension functions!");
    }

    std::cout << "Ray tracing extension functions loaded successfully" << std::endl;
}

// ============================================================================
// Property Querying
// ============================================================================

void RayTracingSystem::queryRTProperties() {
    VkPhysicalDevice physicalDevice = m_Renderer->getPhysicalDevice();

    // Query ray tracing pipeline properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProps{};
    rtPipelineProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    // Query acceleration structure properties
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
    asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    asProps.pNext = &rtPipelineProps;

    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps2.pNext = &asProps;

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProps2);

    // Store pipeline properties
    m_PipelineProps.shaderGroupHandleSize = rtPipelineProps.shaderGroupHandleSize;
    m_PipelineProps.shaderGroupHandleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
    m_PipelineProps.shaderGroupBaseAlignment = rtPipelineProps.shaderGroupBaseAlignment;
    m_PipelineProps.maxRayRecursionDepth = rtPipelineProps.maxRayRecursionDepth;
    m_PipelineProps.maxShaderGroupStride = rtPipelineProps.maxShaderGroupStride;

    // Store acceleration structure properties
    m_ASProps.maxGeometryCount = asProps.maxGeometryCount;
    m_ASProps.maxInstanceCount = asProps.maxInstanceCount;
    m_ASProps.maxPrimitiveCount = asProps.maxPrimitiveCount;
    m_ASProps.minAccelerationStructureScratchOffsetAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;
}



// ============================================================================
// Rendering (Stub implementations for Phase 1)
// ============================================================================

void RayTracingSystem::traceRays(VkCommandBuffer commandBuffer,
                                  const glm::mat4& view,
                                  const glm::mat4& proj,
                                  const glm::vec3& cameraPos,
                                  uint32_t frameIndex) {
    if (!m_Initialized || !m_Settings.enabled) {
        return;
    }

    // Check if we have a valid TLAS
    if (!m_TLAS.isBuilt || m_TLAS.handle == VK_NULL_HANDLE) {
        return;
    }

    // Check if pipeline is ready
    if (m_Pipeline == VK_NULL_HANDLE) {
        return;
    }

    // Update uniform buffer
    updateUniformBuffer(frameIndex, view, proj, cameraPos);

    // Update descriptor sets if TLAS was rebuilt
    if (m_TLASDirty) {
        for (uint32_t i = 0; i < m_Renderer->getMaxFramesInFlight(); i++) {
            updateDescriptorSets(i);
        }
        m_TLASDirty = false;
    }

    // Bind ray tracing pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           m_PipelineLayout, 0, 1, &m_DescriptorSets[frameIndex], 0, nullptr);

    // Trace rays
    pfnCmdTraceRaysKHR(commandBuffer,
                      &m_SBTRegions.raygen,
                      &m_SBTRegions.miss,
                      &m_SBTRegions.hit,
                      &m_SBTRegions.callable,
                      m_OutputWidth,
                      m_OutputHeight,
                      1);  // depth
}

void RayTracingSystem::denoise(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!m_Initialized) {
        return;
    }

    // If denoising is completely disabled, just add barrier for raw RT output
    if (!m_Settings.enableDenoising) {
        VkImageMemoryBarrier barriers[2] = {};

        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = m_ReflectionImage;
        barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        barriers[1] = barriers[0];
        barriers[1].image = m_ShadowImage;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers);
        return;
    }

    // Check if denoiser resources are ready
    if (m_TemporalDenoisePipeline == VK_NULL_HANDLE || m_SpatialDenoisePipeline == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = m_Renderer->getDevice();

    // Calculate dispatch dimensions (8x8 workgroups)
    uint32_t dispatchX = (m_OutputWidth + 7) / 8;
    uint32_t dispatchY = (m_OutputHeight + 7) / 8;

    // Update denoiser uniform buffers
    updateDenoiseDescriptorSets(frameIndex);

    // Transition current RT outputs for compute read
    VkImageMemoryBarrier barriers[2] = {};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = m_ReflectionImage;
    barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    barriers[1] = barriers[0];
    barriers[1].image = m_ShadowImage;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    // ========================================
    // Pass 1: Temporal accumulation
    // Always run temporal pass - it copies current frame to output if disabled
    // ========================================
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_TemporalDenoisePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_DenoisePipelineLayout, 0, 1, &m_DenoiseDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch temporal pass
    vkCmdDispatch(commandBuffer, dispatchX, dispatchY, 1);

    // Barrier: wait for temporal pass to complete before spatial
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].image = m_DenoisedReflectionImage;

    barriers[1] = barriers[0];
    barriers[1].image = m_DenoisedShadowImage;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    // ========================================
    // Pass 2: Spatial filtering
    // ========================================
    if (m_DenoiserSettings.enableSpatial) {
        // Bind spatial denoise pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_SpatialDenoisePipeline);

        // Bind spatial descriptor set (same images, but uses spatial uniform buffer)
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_DenoisePipelineLayout, 0, 1, &m_SpatialDenoiseDescriptorSets[frameIndex], 0, nullptr);

        // Dispatch spatial pass
        vkCmdDispatch(commandBuffer, dispatchX, dispatchY, 1);
    }

    // Final barrier: prepare output for fragment shader sampling AND
    // ensure history buffers are ready for next frame's compute read
    VkImageMemoryBarrier finalBarriers[4] = {};

    // Output images -> fragment shader read
    finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].image = m_DenoisedReflectionImage;
    finalBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    finalBarriers[1] = finalBarriers[0];
    finalBarriers[1].image = m_DenoisedShadowImage;

    // History images -> next frame's compute read
    finalBarriers[2] = finalBarriers[0];
    finalBarriers[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    finalBarriers[2].image = m_HistoryReflectionImage;

    finalBarriers[3] = finalBarriers[2];
    finalBarriers[3].image = m_HistoryShadowImage;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 4, finalBarriers);

    // Store current view-proj for next frame's reprojection
    // (This would be passed in from traceRays in a real implementation)
}

// ============================================================================
// Output Access
// ============================================================================

VkImageView RayTracingSystem::getDenoisedOutput() const {
    // Return denoised output if denoising is enabled and ready
    if (m_Settings.enableDenoising && m_DenoisedReflectionImageView != VK_NULL_HANDLE) {
        return m_DenoisedReflectionImageView;
    }
    // Fallback to raw RT output
    return m_ReflectionImageView;
}

VkDescriptorSet RayTracingSystem::getOutputDescriptorSet(uint32_t frameIndex) const {
    if (frameIndex < m_OutputDescriptorSets.size()) {
        return m_OutputDescriptorSets[frameIndex];
    }
    return VK_NULL_HANDLE;
}

void RayTracingSystem::setGBufferViews(VkImageView depth, VkImageView normal, VkImageView metallicRoughness) {
    m_GBufferDepth = depth;
    m_GBufferNormal = normal;
    m_GBufferMetallicRoughness = metallicRoughness;
}

// ============================================================================
// Helper Functions
// ============================================================================

VkDeviceAddress RayTracingSystem::getBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;
    return pfnGetBufferDeviceAddressKHR(m_Renderer->getDevice(), &addressInfo);
}

bool RayTracingSystem::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice device = m_Renderer->getDevice();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_Renderer->findMemoryType(memRequirements.memoryTypeBits, properties);

    // Add device address flag if buffer usage includes it
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocInfo.pNext = &allocFlagsInfo;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(device, buffer, memory, 0);
    return true;
}

void RayTracingSystem::ensureScratchBuffer(VkDeviceSize requiredSize) {
    if (m_ScratchSize >= requiredSize) {
        return;
    }

    VkDevice device = m_Renderer->getDevice();

    // Cleanup old scratch buffer
    if (m_ScratchBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_ScratchBuffer, nullptr);
    }
    if (m_ScratchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_ScratchMemory, nullptr);
    }

    // Align size to scratch alignment requirement
    VkDeviceSize alignment = m_ASProps.minAccelerationStructureScratchOffsetAlignment;
    requiredSize = (requiredSize + alignment - 1) & ~(alignment - 1);

    // Create new scratch buffer
    createBuffer(requiredSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_ScratchBuffer, m_ScratchMemory);

    m_ScratchSize = requiredSize;
}

std::vector<char> RayTracingSystem::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule RayTracingSystem::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_Renderer->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

// ============================================================================
// Resource Creation (Phase 3)
// ============================================================================

bool RayTracingSystem::createDescriptorSetLayout() {
    VkDevice device = m_Renderer->getDevice();

    // Set 0: All RT resources in a single set
    std::vector<VkDescriptorSetLayoutBinding> set0Bindings = {
        // Binding 0: TLAS
        {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
        // Binding 1: Reflection output image
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
        // Binding 2: Shadow output image
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
        // Binding 3: Uniform buffer (camera, settings)
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, nullptr},
        // Binding 4: Environment cubemap
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, nullptr},
        // Binding 5: Material buffer (per-instance material data)
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
        // Binding 6: Global vertex buffer (for normal interpolation)
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
        // Binding 7: Global index buffer
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
        // Binding 8: Mesh info buffer (vertex/index offsets per mesh)
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(set0Bindings.size());
    layoutInfo.pBindings = set0Bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Output descriptor set layout (for PBR shader to sample RT results)
    std::vector<VkDescriptorSetLayoutBinding> outputBindings = {
        // Binding 0: Reflection sampler
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 1: Shadow sampler
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };

    layoutInfo.bindingCount = static_cast<uint32_t>(outputBindings.size());
    layoutInfo.pBindings = outputBindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_OutputDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    std::cout << "RT descriptor set layouts created" << std::endl;
    return true;
}

bool RayTracingSystem::createDescriptorPool() {
    VkDevice device = m_Renderer->getDevice();
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();

    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, framesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, framesInFlight * 2},     // Reflection + Shadow
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, framesInFlight * 6},  // G-buffer + env map + outputs
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 4}     // Material + vertex + index + mesh info buffers
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight * 3;  // Main RT set + uniform set + output set

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        return false;
    }

    std::cout << "RT descriptor pool created" << std::endl;
    return true;
}

bool RayTracingSystem::createDescriptorSets() {
    VkDevice device = m_Renderer->getDevice();
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();

    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_DescriptorSetLayout);
    m_DescriptorSets.resize(framesInFlight);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Allocate output descriptor sets
    std::vector<VkDescriptorSetLayout> outputLayouts(framesInFlight, m_OutputDescriptorSetLayout);
    m_OutputDescriptorSets.resize(framesInFlight);

    allocInfo.pSetLayouts = outputLayouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, m_OutputDescriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Update descriptor sets for each frame
    for (uint32_t i = 0; i < framesInFlight; i++) {
        updateDescriptorSets(i);
    }

    std::cout << "RT descriptor sets created and updated" << std::endl;
    return true;
}

bool RayTracingSystem::createRTPipeline() {
    VkDevice device = m_Renderer->getDevice();

    // Load shader modules
    auto raygenCode = readShaderFile("shaders/raytracing/raygen.rgen.spv");
    auto missCode = readShaderFile("shaders/raytracing/miss.rmiss.spv");
    auto missShadowCode = readShaderFile("shaders/raytracing/miss_shadow.rmiss.spv");
    auto chitCode = readShaderFile("shaders/raytracing/closesthit.rchit.spv");

    VkShaderModule raygenModule = createShaderModule(raygenCode);
    VkShaderModule missModule = createShaderModule(missCode);
    VkShaderModule missShadowModule = createShaderModule(missShadowCode);
    VkShaderModule chitModule = createShaderModule(chitCode);

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages(4);

    // Ray generation shader
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    shaderStages[0].module = raygenModule;
    shaderStages[0].pName = "main";

    // Miss shader (reflection)
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shaderStages[1].module = missModule;
    shaderStages[1].pName = "main";

    // Miss shader (shadow)
    shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shaderStages[2].module = missShadowModule;
    shaderStages[2].pName = "main";

    // Closest hit shader
    shaderStages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    shaderStages[3].module = chitModule;
    shaderStages[3].pName = "main";

    // Shader groups
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(RT_SHADER_GROUP_COUNT);

    // Ray generation group
    shaderGroups[RT_SHADER_GROUP_RAYGEN].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[RT_SHADER_GROUP_RAYGEN].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[RT_SHADER_GROUP_RAYGEN].generalShader = 0;
    shaderGroups[RT_SHADER_GROUP_RAYGEN].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_RAYGEN].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_RAYGEN].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Miss group (reflection)
    shaderGroups[RT_SHADER_GROUP_MISS].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS].generalShader = 1;
    shaderGroups[RT_SHADER_GROUP_MISS].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Miss group (shadow)
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].generalShader = 2;
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_MISS_SHADOW].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Hit group (closest hit)
    shaderGroups[RT_SHADER_GROUP_HIT].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[RT_SHADER_GROUP_HIT].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroups[RT_SHADER_GROUP_HIT].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_HIT].closestHitShader = 3;
    shaderGroups[RT_SHADER_GROUP_HIT].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[RT_SHADER_GROUP_HIT].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Pipeline layout
    // We need multiple descriptor set layouts for different sets
    std::vector<VkDescriptorSetLayout> setLayouts = {
        m_DescriptorSetLayout  // Set 0: TLAS + outputs
        // Set 1-3 will use layouts from the renderer (uniforms, G-buffer, environment)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, raygenModule, nullptr);
        vkDestroyShaderModule(device, missModule, nullptr);
        vkDestroyShaderModule(device, missShadowModule, nullptr);
        vkDestroyShaderModule(device, chitModule, nullptr);
        return false;
    }

    // Create ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    pipelineInfo.pGroups = shaderGroups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = std::min(m_PipelineProps.maxRayRecursionDepth, 2u);
    pipelineInfo.layout = m_PipelineLayout;

    VkResult result = pfnCreateRayTracingPipelinesKHR(
        device,
        VK_NULL_HANDLE,  // deferred operation
        VK_NULL_HANDLE,  // pipeline cache
        1,
        &pipelineInfo,
        nullptr,
        &m_Pipeline
    );

    // Cleanup shader modules
    vkDestroyShaderModule(device, raygenModule, nullptr);
    vkDestroyShaderModule(device, missModule, nullptr);
    vkDestroyShaderModule(device, missShadowModule, nullptr);
    vkDestroyShaderModule(device, chitModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }

    std::cout << "RT pipeline created" << std::endl;
    return true;
}

bool RayTracingSystem::createShaderBindingTable() {
    VkDevice device = m_Renderer->getDevice();

    // Get shader group handles
    uint32_t handleSize = m_PipelineProps.shaderGroupHandleSize;
    uint32_t handleAlignment = m_PipelineProps.shaderGroupHandleAlignment;
    uint32_t baseAlignment = m_PipelineProps.shaderGroupBaseAlignment;

    // Align handle size
    uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    // Calculate SBT sizes
    uint32_t raygenSize = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
    uint32_t missCount = 2;  // Reflection + shadow miss
    uint32_t missSize = missCount * handleSizeAligned;
    missSize = (missSize + baseAlignment - 1) & ~(baseAlignment - 1);
    uint32_t hitCount = 1;   // One hit group
    uint32_t hitSize = hitCount * handleSizeAligned;
    hitSize = (hitSize + baseAlignment - 1) & ~(baseAlignment - 1);

    VkDeviceSize sbtSize = raygenSize + missSize + hitSize;

    // Get all shader group handles
    uint32_t groupCount = RT_SHADER_GROUP_COUNT;
    std::vector<uint8_t> shaderHandles(groupCount * handleSize);

    if (pfnGetRayTracingShaderGroupHandlesKHR(
            device, m_Pipeline, 0, groupCount, shaderHandles.size(), shaderHandles.data()) != VK_SUCCESS) {
        return false;
    }

    // Create SBT buffer
    createBuffer(sbtSize,
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_SBTBuffer, m_SBTMemory);

    // Map and fill SBT
    void* mapped;
    vkMapMemory(device, m_SBTMemory, 0, sbtSize, 0, &mapped);
    uint8_t* sbtData = static_cast<uint8_t*>(mapped);

    // Ray generation region
    memcpy(sbtData, shaderHandles.data() + RT_SHADER_GROUP_RAYGEN * handleSize, handleSize);

    // Miss region
    uint8_t* missData = sbtData + raygenSize;
    memcpy(missData, shaderHandles.data() + RT_SHADER_GROUP_MISS * handleSize, handleSize);
    memcpy(missData + handleSizeAligned, shaderHandles.data() + RT_SHADER_GROUP_MISS_SHADOW * handleSize, handleSize);

    // Hit region
    uint8_t* hitData = sbtData + raygenSize + missSize;
    memcpy(hitData, shaderHandles.data() + RT_SHADER_GROUP_HIT * handleSize, handleSize);

    vkUnmapMemory(device, m_SBTMemory);

    // Get SBT device addresses
    VkDeviceAddress sbtAddress = getBufferDeviceAddress(m_SBTBuffer);

    // Setup SBT regions
    m_SBTRegions.raygen.deviceAddress = sbtAddress;
    m_SBTRegions.raygen.stride = raygenSize;
    m_SBTRegions.raygen.size = raygenSize;

    m_SBTRegions.miss.deviceAddress = sbtAddress + raygenSize;
    m_SBTRegions.miss.stride = handleSizeAligned;
    m_SBTRegions.miss.size = missSize;

    m_SBTRegions.hit.deviceAddress = sbtAddress + raygenSize + missSize;
    m_SBTRegions.hit.stride = handleSizeAligned;
    m_SBTRegions.hit.size = hitSize;

    m_SBTRegions.callable.deviceAddress = 0;
    m_SBTRegions.callable.stride = 0;
    m_SBTRegions.callable.size = 0;

    std::cout << "Shader Binding Table created" << std::endl;
    return true;
}

bool RayTracingSystem::createOutputImages() {
    VkDevice device = m_Renderer->getDevice();

    // Reflection output (RGBA16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    m_ReflectionImage, m_ReflectionMemory)) {
        return false;
    }
    m_ReflectionImageView = createImageView(m_ReflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Shadow output (R16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    m_ShadowImage, m_ShadowMemory)) {
        return false;
    }
    m_ShadowImageView = createImageView(m_ShadowImage, VK_FORMAT_R16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Transition images to general layout
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.image = m_ReflectionImage;
    vkCmdPipelineBarrier(cmdBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.image = m_ShadowImage;
    vkCmdPipelineBarrier(cmdBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_Renderer->endSingleTimeCommands(cmdBuffer);

    // Create sampler for output images
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_OutputSampler) != VK_SUCCESS) {
        std::cerr << "Failed to create RT output sampler" << std::endl;
        return false;
    }

    std::cout << "RT output images created (" << m_OutputWidth << "x" << m_OutputHeight << ")" << std::endl;
    return true;
}

bool RayTracingSystem::createImage(uint32_t width, uint32_t height, VkFormat format,
                                    VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkDevice device = m_Renderer->getDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_Renderer->findMemoryType(memRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        return false;
    }

    vkBindImageMemory(device, image, memory, 0);
    return true;
}

VkImageView RayTracingSystem::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkDevice device = m_Renderer->getDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RT image view!");
    }

    return imageView;
}

bool RayTracingSystem::createUniformBuffers() {
    VkDevice device = m_Renderer->getDevice();
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();

    VkDeviceSize bufferSize = sizeof(RTUniformBuffer);

    m_UniformBuffers.resize(framesInFlight);
    m_UniformBuffersMemory.resize(framesInFlight);
    m_UniformBuffersMapped.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        createBuffer(bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_UniformBuffers[i], m_UniformBuffersMemory[i]);

        vkMapMemory(device, m_UniformBuffersMemory[i], 0, bufferSize, 0, &m_UniformBuffersMapped[i]);
    }

    std::cout << "RT uniform buffers created" << std::endl;
    return true;
}

bool RayTracingSystem::createMaterialBuffer() {
    VkDevice device = m_Renderer->getDevice();

    // Create material buffer large enough for max instances
    VkDeviceSize bufferSize = RT_MAX_INSTANCES * sizeof(RTMaterialData);

    if (!createBuffer(bufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_MaterialBuffer, m_MaterialMemory)) {
        return false;
    }

    vkMapMemory(device, m_MaterialMemory, 0, bufferSize, 0, &m_MaterialBufferMapped);

    // Initialize with default materials
    RTMaterialData defaultMaterial{};
    defaultMaterial.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    defaultMaterial.metallic = 0.0f;
    defaultMaterial.roughness = 0.5f;
    defaultMaterial.ao = 1.0f;
    defaultMaterial.emissive = 0.0f;
    defaultMaterial.albedoTexIndex = -1;
    defaultMaterial.normalTexIndex = -1;
    defaultMaterial.metallicRoughnessTexIndex = -1;
    defaultMaterial.emissiveTexIndex = -1;

    RTMaterialData* materials = static_cast<RTMaterialData*>(m_MaterialBufferMapped);
    for (uint32_t i = 0; i < RT_MAX_INSTANCES; i++) {
        materials[i] = defaultMaterial;
    }

    std::cout << "RT material buffer created (" << RT_MAX_INSTANCES << " slots)" << std::endl;
    return true;
}

bool RayTracingSystem::createGeometryBuffers() {
    VkDevice device = m_Renderer->getDevice();

    // Estimate max geometry size (can grow dynamically later if needed)
    // 256K vertices * sizeof(RTVertex) = ~16MB
    // 1M indices * sizeof(uint32_t) = ~4MB
    constexpr uint32_t MAX_VERTICES = 256 * 1024;
    constexpr uint32_t MAX_INDICES = 1024 * 1024;
    constexpr uint32_t MAX_MESHES = 256;

    VkDeviceSize vertexBufferSize = MAX_VERTICES * sizeof(RTVertex);
    VkDeviceSize indexBufferSize = MAX_INDICES * sizeof(uint32_t);
    VkDeviceSize meshInfoBufferSize = MAX_MESHES * sizeof(RTMeshInfo);

    // Create global vertex buffer
    if (!createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_GeometryVertexBuffer, m_GeometryVertexMemory)) {
        std::cerr << "Failed to create RT global vertex buffer" << std::endl;
        return false;
    }

    // Create global index buffer
    if (!createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_GeometryIndexBuffer, m_GeometryIndexMemory)) {
        std::cerr << "Failed to create RT global index buffer" << std::endl;
        return false;
    }

    // Create mesh info buffer (host visible for easy updates)
    if (!createBuffer(meshInfoBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_MeshInfoBuffer, m_MeshInfoMemory)) {
        std::cerr << "Failed to create RT mesh info buffer" << std::endl;
        return false;
    }

    vkMapMemory(device, m_MeshInfoMemory, 0, meshInfoBufferSize, 0, &m_MeshInfoMapped);

    // Initialize mesh info with zeros
    memset(m_MeshInfoMapped, 0, meshInfoBufferSize);

    m_TotalVertexCount = 0;
    m_TotalIndexCount = 0;

    std::cout << "RT geometry buffers created (max " << MAX_VERTICES << " vertices, "
              << MAX_INDICES << " indices, " << MAX_MESHES << " meshes)" << std::endl;
    return true;
}

void RayTracingSystem::updateGeometryBuffers() {
    // This is called after BLAS creation to copy mesh geometry into global buffers
    // Each BLAS already has vertex/index data in its own buffers, so we copy from there

    if (m_BLASMap.empty()) return;

    VkDevice device = m_Renderer->getDevice();
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();

    m_TotalVertexCount = 0;
    m_TotalIndexCount = 0;

    RTMeshInfo* meshInfos = static_cast<RTMeshInfo*>(m_MeshInfoMapped);

    for (auto& [meshId, blas] : m_BLASMap) {
        if (!blas.isBuilt || blas.vertexBuffer == VK_NULL_HANDLE) continue;

        // Record offset for this mesh
        blas.globalVertexOffset = m_TotalVertexCount;
        blas.globalIndexOffset = m_TotalIndexCount;

        // Update mesh info buffer
        if (meshId < 256) {
            meshInfos[meshId].vertexOffset = m_TotalVertexCount;
            meshInfos[meshId].indexOffset = m_TotalIndexCount;
            meshInfos[meshId].vertexCount = blas.vertexCount;
            meshInfos[meshId].indexCount = blas.indexCount;
        }

        // Copy vertex data (need to create staging buffer with RTVertex format)
        // For now, we copy raw vertex data and handle stride in shader
        VkBufferCopy vertexCopy{};
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = m_TotalVertexCount * sizeof(Vertex);
        vertexCopy.size = blas.vertexCount * sizeof(Vertex);
        vkCmdCopyBuffer(cmdBuffer, blas.vertexBuffer, m_GeometryVertexBuffer, 1, &vertexCopy);

        // Copy index data
        VkBufferCopy indexCopy{};
        indexCopy.srcOffset = 0;
        indexCopy.dstOffset = m_TotalIndexCount * sizeof(uint32_t);
        indexCopy.size = blas.indexCount * sizeof(uint32_t);
        vkCmdCopyBuffer(cmdBuffer, blas.indexBuffer, m_GeometryIndexBuffer, 1, &indexCopy);

        m_TotalVertexCount += blas.vertexCount;
        m_TotalIndexCount += blas.indexCount;
    }

    m_Renderer->endSingleTimeCommands(cmdBuffer);

    std::cout << "RT geometry buffers updated (" << m_TotalVertexCount << " vertices, "
              << m_TotalIndexCount << " indices)" << std::endl;
}

void RayTracingSystem::updateDescriptorSets(uint32_t frameIndex) {
    VkDevice device = m_Renderer->getDevice();

    std::vector<VkWriteDescriptorSet> writes;

    // Only update if TLAS is built
    if (m_TLAS.isBuilt && m_TLAS.handle != VK_NULL_HANDLE) {
        // Write TLAS descriptor
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
        asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &m_TLAS.handle;

        VkWriteDescriptorSet asDescWrite{};
        asDescWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        asDescWrite.pNext = &asWrite;
        asDescWrite.dstSet = m_DescriptorSets[frameIndex];
        asDescWrite.dstBinding = 0;
        asDescWrite.dstArrayElement = 0;
        asDescWrite.descriptorCount = 1;
        asDescWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        writes.push_back(asDescWrite);
    }

    // Write output images
    VkDescriptorImageInfo reflectionInfo{};
    reflectionInfo.imageView = m_ReflectionImageView;
    reflectionInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet reflectionWrite{};
    reflectionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    reflectionWrite.dstSet = m_DescriptorSets[frameIndex];
    reflectionWrite.dstBinding = 1;
    reflectionWrite.dstArrayElement = 0;
    reflectionWrite.descriptorCount = 1;
    reflectionWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    reflectionWrite.pImageInfo = &reflectionInfo;

    writes.push_back(reflectionWrite);

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageView = m_ShadowImageView;
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet shadowWrite{};
    shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowWrite.dstSet = m_DescriptorSets[frameIndex];
    shadowWrite.dstBinding = 2;
    shadowWrite.dstArrayElement = 0;
    shadowWrite.descriptorCount = 1;
    shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    shadowWrite.pImageInfo = &shadowInfo;

    writes.push_back(shadowWrite);

    // Write uniform buffer (binding 3)
    if (frameIndex < m_UniformBuffers.size() && m_UniformBuffers[frameIndex] != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = m_UniformBuffers[frameIndex];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(RTUniformBuffer);

        VkWriteDescriptorSet uniformWrite{};
        uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWrite.dstSet = m_DescriptorSets[frameIndex];
        uniformWrite.dstBinding = 3;
        uniformWrite.dstArrayElement = 0;
        uniformWrite.descriptorCount = 1;
        uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.pBufferInfo = &uniformInfo;

        writes.push_back(uniformWrite);
    }

    // Write environment map (binding 4) - always write, use fallback if IBL not ready
    {
        VkDescriptorImageInfo envMapInfo{};

        if (m_IBLSystem && m_IBLSystem->isReady() && m_IBLSystem->getPrefilterMap()) {
            envMapInfo.sampler = m_IBLSystem->getPrefilterMap()->getSampler();
            envMapInfo.imageView = m_IBLSystem->getPrefilterMap()->getImageView();
            envMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else {
            // Use default texture as fallback
            auto defaultTex = m_Renderer->getDefaultTexture();
            if (defaultTex) {
                envMapInfo.sampler = defaultTex->getSampler();
                envMapInfo.imageView = defaultTex->getImageView();
                envMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }

        // Only write if we have a valid sampler
        if (envMapInfo.sampler != VK_NULL_HANDLE && envMapInfo.imageView != VK_NULL_HANDLE) {
            VkWriteDescriptorSet envMapWrite{};
            envMapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            envMapWrite.dstSet = m_DescriptorSets[frameIndex];
            envMapWrite.dstBinding = 4;
            envMapWrite.dstArrayElement = 0;
            envMapWrite.descriptorCount = 1;
            envMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            envMapWrite.pImageInfo = &envMapInfo;

            writes.push_back(envMapWrite);
        }
    }

    // Write material buffer (binding 5)
    if (m_MaterialBuffer != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo materialBufferInfo{};
        materialBufferInfo.buffer = m_MaterialBuffer;
        materialBufferInfo.offset = 0;
        materialBufferInfo.range = RT_MAX_INSTANCES * sizeof(RTMaterialData);

        VkWriteDescriptorSet materialWrite{};
        materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        materialWrite.dstSet = m_DescriptorSets[frameIndex];
        materialWrite.dstBinding = 5;
        materialWrite.dstArrayElement = 0;
        materialWrite.descriptorCount = 1;
        materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialWrite.pBufferInfo = &materialBufferInfo;

        writes.push_back(materialWrite);
    }

    // Write global vertex buffer (binding 6)
    if (m_GeometryVertexBuffer != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo vertexBufferInfo{};
        vertexBufferInfo.buffer = m_GeometryVertexBuffer;
        vertexBufferInfo.offset = 0;
        vertexBufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet vertexWrite{};
        vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vertexWrite.dstSet = m_DescriptorSets[frameIndex];
        vertexWrite.dstBinding = 6;
        vertexWrite.dstArrayElement = 0;
        vertexWrite.descriptorCount = 1;
        vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertexWrite.pBufferInfo = &vertexBufferInfo;

        writes.push_back(vertexWrite);
    }

    // Write global index buffer (binding 7)
    if (m_GeometryIndexBuffer != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo indexBufferInfo{};
        indexBufferInfo.buffer = m_GeometryIndexBuffer;
        indexBufferInfo.offset = 0;
        indexBufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet indexWrite{};
        indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        indexWrite.dstSet = m_DescriptorSets[frameIndex];
        indexWrite.dstBinding = 7;
        indexWrite.dstArrayElement = 0;
        indexWrite.descriptorCount = 1;
        indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        indexWrite.pBufferInfo = &indexBufferInfo;

        writes.push_back(indexWrite);
    }

    // Write mesh info buffer (binding 8)
    if (m_MeshInfoBuffer != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo meshInfoBufferInfo{};
        meshInfoBufferInfo.buffer = m_MeshInfoBuffer;
        meshInfoBufferInfo.offset = 0;
        meshInfoBufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet meshInfoWrite{};
        meshInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        meshInfoWrite.dstSet = m_DescriptorSets[frameIndex];
        meshInfoWrite.dstBinding = 8;
        meshInfoWrite.dstArrayElement = 0;
        meshInfoWrite.descriptorCount = 1;
        meshInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        meshInfoWrite.pBufferInfo = &meshInfoBufferInfo;

        writes.push_back(meshInfoWrite);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Update output descriptor sets (for PBR shader sampling)
    // Use denoised output if denoising is enabled and available, otherwise use raw RT output
    if (m_OutputSampler != VK_NULL_HANDLE && frameIndex < m_OutputDescriptorSets.size()) {
        std::vector<VkWriteDescriptorSet> outputWrites;

        // Choose reflection image view based on denoising state
        VkImageView reflectionView = m_ReflectionImageView;
        if (m_Settings.enableDenoising && m_DenoisedReflectionImageView != VK_NULL_HANDLE) {
            reflectionView = m_DenoisedReflectionImageView;
        }

        VkDescriptorImageInfo reflectionSampleInfo{};
        reflectionSampleInfo.sampler = m_OutputSampler;
        reflectionSampleInfo.imageView = reflectionView;
        reflectionSampleInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet reflectionSampleWrite{};
        reflectionSampleWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        reflectionSampleWrite.dstSet = m_OutputDescriptorSets[frameIndex];
        reflectionSampleWrite.dstBinding = 0;
        reflectionSampleWrite.dstArrayElement = 0;
        reflectionSampleWrite.descriptorCount = 1;
        reflectionSampleWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        reflectionSampleWrite.pImageInfo = &reflectionSampleInfo;

        outputWrites.push_back(reflectionSampleWrite);

        // Choose shadow image view based on denoising state
        VkImageView shadowView = m_ShadowImageView;
        if (m_Settings.enableDenoising && m_DenoisedShadowImageView != VK_NULL_HANDLE) {
            shadowView = m_DenoisedShadowImageView;
        }

        VkDescriptorImageInfo shadowSampleInfo{};
        shadowSampleInfo.sampler = m_OutputSampler;
        shadowSampleInfo.imageView = shadowView;
        shadowSampleInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet shadowSampleWrite{};
        shadowSampleWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowSampleWrite.dstSet = m_OutputDescriptorSets[frameIndex];
        shadowSampleWrite.dstBinding = 1;
        shadowSampleWrite.dstArrayElement = 0;
        shadowSampleWrite.descriptorCount = 1;
        shadowSampleWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowSampleWrite.pImageInfo = &shadowSampleInfo;

        outputWrites.push_back(shadowSampleWrite);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(outputWrites.size()), outputWrites.data(), 0, nullptr);
    }
}

void RayTracingSystem::updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view,
                                            const glm::mat4& proj, const glm::vec3& cameraPos) {
    RTUniformBuffer ubo{};
    ubo.viewInverse = glm::inverse(view);
    ubo.projInverse = glm::inverse(proj);
    ubo.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Get light from scene (first directional light found)
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)); // Default
    glm::vec3 lightCol = glm::vec3(1.0f, 0.98f, 0.95f); // Default
    float lightIntensity = 1.0f;

    Scene* scene = m_Renderer->getScene();
    if (scene) {
        const auto& lights = scene->getLights();
        for (const auto& light : lights) {
            if (light.isDirectional) {
                lightDir = glm::normalize(light.position); // For directional, position is direction
                lightCol = light.color;
                lightIntensity = light.intensity;
                break; // Use first directional light
            }
        }
    }

    ubo.lightDirection = glm::vec4(lightDir, lightIntensity);
    ubo.lightColor = glm::vec4(lightCol, 1.0f);

    ubo.frameNumber = m_FrameNumber++;
    ubo.samplesPerPixel = m_Settings.samplesPerPixel;
    ubo.maxBounces = m_Settings.maxBounces;
    ubo.reflectionBias = m_Settings.reflectionBias;
    ubo.shadowBias = m_Settings.shadowBias;
    ubo.shadowSoftness = m_Settings.shadowSoftness;

    ubo.flags = 0;
    if (m_Settings.enableReflections) ubo.flags |= 1;
    if (m_Settings.enableSoftShadows) ubo.flags |= 2;
    if (m_IBLEnabled && m_IBLSystem && m_IBLSystem->isReady()) ubo.flags |= 4;  // Bit 2: IBL enabled

    ubo.debugMode = m_Settings.debugMode;

    memcpy(m_UniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

// ============================================================================
// BLAS Management (Phase 2)
// ============================================================================

bool RayTracingSystem::createBLAS(const std::shared_ptr<Mesh>& mesh, uint32_t meshId) {
    if (!mesh || m_BLASMap.find(meshId) != m_BLASMap.end()) {
        return false; // Already exists or invalid mesh
    }

    VkDevice device = m_Renderer->getDevice();

    // Get vertex and index counts from the mesh
    uint32_t vertexCount = mesh->vertexCount;
    uint32_t indexCount = mesh->indexCount;

    if (vertexCount == 0 || indexCount == 0) {
        std::cerr << "Mesh " << meshId << " has no geometry data (vertexCount="
                  << vertexCount << ", indexCount=" << indexCount << ")" << std::endl;
        return false;
    }

    BLASInfo blas;
    blas.meshId = meshId;
    blas.indexCount = indexCount;
    blas.vertexCount = vertexCount;

    // Create RT-compatible vertex buffer by copying from existing mesh buffer
    // We need to get the mesh's buffer device addresses, but they weren't created with
    // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. So we create new RT buffers.

    // For now, we'll create placeholder geometry that uses the mesh's existing
    // vertex/index buffers through vkCmdCopyBuffer to RT-compatible buffers.

    // Calculate buffer sizes (Vertex is 60 bytes based on CommonVertex.h)
    VkDeviceSize vertexBufferSize = vertexCount * sizeof(Vertex);
    VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

    // Create RT vertex buffer with device address support
    // Note: TRANSFER_SRC_BIT needed to copy to global geometry buffer for shader access
    if (!createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     blas.vertexBuffer, blas.vertexMemory)) {
        std::cerr << "Failed to create RT vertex buffer for mesh " << meshId << std::endl;
        return false;
    }
    blas.vertexBufferAddress = getBufferDeviceAddress(blas.vertexBuffer);

    // Create RT index buffer with device address support
    // Note: TRANSFER_SRC_BIT needed to copy to global geometry buffer for shader access
    if (!createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     blas.indexBuffer, blas.indexMemory)) {
        std::cerr << "Failed to create RT index buffer for mesh " << meshId << std::endl;
        vkDestroyBuffer(device, blas.vertexBuffer, nullptr);
        vkFreeMemory(device, blas.vertexMemory, nullptr);
        return false;
    }
    blas.indexBufferAddress = getBufferDeviceAddress(blas.indexBuffer);

    // Copy geometry data from mesh's existing buffers to RT buffers
    VkBuffer srcVertexBuffer = mesh->getVertexBuffer();
    VkBuffer srcIndexBuffer = mesh->getIndexBuffer();

    if (srcVertexBuffer != VK_NULL_HANDLE && srcIndexBuffer != VK_NULL_HANDLE) {
        VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();

        // Copy vertex buffer
        VkBufferCopy vertexCopy{};
        vertexCopy.size = vertexBufferSize;
        vkCmdCopyBuffer(cmdBuffer, srcVertexBuffer, blas.vertexBuffer, 1, &vertexCopy);

        // Copy index buffer
        VkBufferCopy indexCopy{};
        indexCopy.size = indexBufferSize;
        vkCmdCopyBuffer(cmdBuffer, srcIndexBuffer, blas.indexBuffer, 1, &indexCopy);

        m_Renderer->endSingleTimeCommands(cmdBuffer);
    } else {
        std::cerr << "Warning: Mesh " << meshId << " has no GPU buffers, BLAS will be empty" << std::endl;
    }

    // Setup acceleration structure geometry
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // Opaque geometry for performance
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = blas.vertexBufferAddress;
    geometry.geometry.triangles.vertexStride = sizeof(Vertex);
    geometry.geometry.triangles.maxVertex = vertexCount - 1;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = blas.indexBufferAddress;
    geometry.geometry.triangles.transformData.deviceAddress = 0; // No transform

    // Get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primitiveCount = indexCount / 3;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizesKHR(
        device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    // Create acceleration structure buffer
    if (!createBuffer(sizeInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     blas.buffer, blas.memory)) {
        std::cerr << "Failed to create BLAS buffer for mesh " << meshId << std::endl;
        vkDestroyBuffer(device, blas.vertexBuffer, nullptr);
        vkFreeMemory(device, blas.vertexMemory, nullptr);
        vkDestroyBuffer(device, blas.indexBuffer, nullptr);
        vkFreeMemory(device, blas.indexMemory, nullptr);
        return false;
    }

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = blas.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (pfnCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blas.handle) != VK_SUCCESS) {
        std::cerr << "Failed to create BLAS for mesh " << meshId << std::endl;
        vkDestroyBuffer(device, blas.buffer, nullptr);
        vkFreeMemory(device, blas.memory, nullptr);
        vkDestroyBuffer(device, blas.vertexBuffer, nullptr);
        vkFreeMemory(device, blas.vertexMemory, nullptr);
        vkDestroyBuffer(device, blas.indexBuffer, nullptr);
        vkFreeMemory(device, blas.indexMemory, nullptr);
        return false;
    }

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = blas.handle;
    blas.deviceAddress = pfnGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

    // Ensure scratch buffer is large enough
    ensureScratchBuffer(sizeInfo.buildScratchSize);

    // Build the BLAS
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeometryInfo.dstAccelerationStructure = blas.handle;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries = &geometry;
    buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(m_ScratchBuffer);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = primitiveCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[] = { &buildRangeInfo };

    // Build using a single-time command buffer
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();
    pfnCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildGeometryInfo, pBuildRangeInfos);
    m_Renderer->endSingleTimeCommands(cmdBuffer);

    blas.isBuilt = true;
    m_BLASMap[meshId] = std::move(blas);

    std::cout << "Created BLAS for mesh " << meshId << " with " << primitiveCount << " triangles" << std::endl;
    return true;
}

void RayTracingSystem::destroyBLAS(uint32_t meshId) {
    auto it = m_BLASMap.find(meshId);
    if (it == m_BLASMap.end()) {
        return;
    }

    VkDevice device = m_Renderer->getDevice();
    BLASInfo& blas = it->second;

    // Destroy acceleration structure
    if (blas.handle != VK_NULL_HANDLE && pfnDestroyAccelerationStructureKHR) {
        pfnDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
    }

    // Destroy AS buffer
    if (blas.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blas.buffer, nullptr);
    }
    if (blas.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blas.memory, nullptr);
    }

    // Destroy geometry buffers
    if (blas.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blas.vertexBuffer, nullptr);
    }
    if (blas.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blas.vertexMemory, nullptr);
    }
    if (blas.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blas.indexBuffer, nullptr);
    }
    if (blas.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blas.indexMemory, nullptr);
    }

    m_BLASMap.erase(it);
}

const BLASInfo* RayTracingSystem::getBLAS(uint32_t meshId) const {
    auto it = m_BLASMap.find(meshId);
    if (it != m_BLASMap.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// TLAS Management (Phase 2)
// ============================================================================

bool RayTracingSystem::buildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances) {
    if (instances.empty()) {
        return false;
    }

    VkDevice device = m_Renderer->getDevice();

    // Cleanup existing TLAS if any
    if (m_TLAS.handle != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (pfnDestroyAccelerationStructureKHR) {
            pfnDestroyAccelerationStructureKHR(device, m_TLAS.handle, nullptr);
        }
        if (m_TLAS.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_TLAS.buffer, nullptr);
        }
        if (m_TLAS.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_TLAS.memory, nullptr);
        }
        m_TLAS.handle = VK_NULL_HANDLE;
        m_TLAS.buffer = VK_NULL_HANDLE;
        m_TLAS.memory = VK_NULL_HANDLE;
    }

    m_TLAS.instanceCount = static_cast<uint32_t>(instances.size());

    // Create instance buffer
    VkDeviceSize instanceBufferSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);

    // Cleanup old instance buffer if exists
    if (m_TLAS.instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_TLAS.instanceBuffer, nullptr);
    }
    if (m_TLAS.instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_TLAS.instanceMemory, nullptr);
    }

    // Create staging buffer for instance data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(instanceBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingMemory);

    // Copy instance data to staging buffer
    void* data;
    vkMapMemory(device, stagingMemory, 0, instanceBufferSize, 0, &data);
    memcpy(data, instances.data(), instanceBufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Create device-local instance buffer
    createBuffer(instanceBufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_TLAS.instanceBuffer, m_TLAS.instanceMemory);

    // Copy from staging to device buffer
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = instanceBufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, m_TLAS.instanceBuffer, 1, &copyRegion);
    m_Renderer->endSingleTimeCommands(cmdBuffer);

    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Setup geometry for TLAS
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = getBufferDeviceAddress(m_TLAS.instanceBuffer);

    // Get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t instanceCount = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizesKHR(
        device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo);

    // Create TLAS buffer
    createBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_TLAS.buffer, m_TLAS.memory);

    // Create TLAS
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_TLAS.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (pfnCreateAccelerationStructureKHR(device, &createInfo, nullptr, &m_TLAS.handle) != VK_SUCCESS) {
        std::cerr << "Failed to create TLAS" << std::endl;
        return false;
    }

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_TLAS.handle;
    m_TLAS.deviceAddress = pfnGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

    // Ensure scratch buffer is large enough
    ensureScratchBuffer(sizeInfo.buildScratchSize);

    // Build TLAS
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                              VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeometryInfo.dstAccelerationStructure = m_TLAS.handle;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries = &geometry;
    buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(m_ScratchBuffer);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = instanceCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[] = { &buildRangeInfo };

    cmdBuffer = m_Renderer->beginSingleTimeCommands();
    pfnCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildGeometryInfo, pBuildRangeInfos);
    m_Renderer->endSingleTimeCommands(cmdBuffer);

    m_TLAS.isBuilt = true;
    m_TLASDirty = true;  // Signal that descriptors need to be updated with the new TLAS

    std::cout << "Built TLAS with " << instanceCount << " instances" << std::endl;
    return true;
}

bool RayTracingSystem::updateTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances) {
    if (!m_TLAS.isBuilt || instances.empty()) {
        return buildTLAS(instances);
    }

    // If instance count changed, rebuild
    if (instances.size() != m_TLAS.instanceCount) {
        return buildTLAS(instances);
    }

    VkDevice device = m_Renderer->getDevice();

    // Update instance data
    VkDeviceSize instanceBufferSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(instanceBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingMemory);

    // Copy instance data
    void* data;
    vkMapMemory(device, stagingMemory, 0, instanceBufferSize, 0, &data);
    memcpy(data, instances.data(), instanceBufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Copy to device buffer
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = instanceBufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, m_TLAS.instanceBuffer, 1, &copyRegion);
    m_Renderer->endSingleTimeCommands(cmdBuffer);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Setup geometry
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = getBufferDeviceAddress(m_TLAS.instanceBuffer);

    // Refit the TLAS (update mode)
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                              VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    buildGeometryInfo.srcAccelerationStructure = m_TLAS.handle;
    buildGeometryInfo.dstAccelerationStructure = m_TLAS.handle;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries = &geometry;
    buildGeometryInfo.scratchData.deviceAddress = getBufferDeviceAddress(m_ScratchBuffer);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = static_cast<uint32_t>(instances.size());
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[] = { &buildRangeInfo };

    cmdBuffer = m_Renderer->beginSingleTimeCommands();
    pfnCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildGeometryInfo, pBuildRangeInfos);
    m_Renderer->endSingleTimeCommands(cmdBuffer);

    // Note: For updates (refit), we don't need to update descriptors since TLAS handle hasn't changed
    // m_TLASDirty remains false for refits
    return true;
}

// ============================================================================
// Scene Update (Phase 2)
// ============================================================================

void RayTracingSystem::updateScene(Scene* scene) {
    if (!m_Initialized || !scene) {
        return;
    }

    const auto& meshInstances = scene->getMeshInstances();
    if (meshInstances.empty()) {
        return;
    }

    // Track which meshes we've seen (using mesh pointer address as ID)
    std::unordered_map<uintptr_t, uint32_t> meshToId;
    uint32_t nextMeshId = 0;

    // First pass: create BLAS for each unique mesh
    for (const auto& instance : meshInstances) {
        if (!instance.mesh) continue;

        uintptr_t meshPtr = reinterpret_cast<uintptr_t>(instance.mesh.get());
        if (meshToId.find(meshPtr) == meshToId.end()) {
            meshToId[meshPtr] = nextMeshId;

            // Create BLAS if not already exists
            if (m_BLASMap.find(nextMeshId) == m_BLASMap.end()) {
                createBLAS(instance.mesh, nextMeshId);
            }
            nextMeshId++;
        }
    }

    // Second pass: build TLAS instances
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(meshInstances.size());

    uint32_t instanceIndex = 0;
    for (const auto& instance : meshInstances) {
        if (!instance.mesh) continue;

        uintptr_t meshPtr = reinterpret_cast<uintptr_t>(instance.mesh.get());
        auto meshIdIt = meshToId.find(meshPtr);
        if (meshIdIt == meshToId.end()) continue;

        uint32_t meshId = meshIdIt->second;
        const BLASInfo* blas = getBLAS(meshId);
        if (!blas || !blas->isBuilt) continue;

        // Get transform matrix
        glm::mat4 modelMatrix = instance.transform.getModelMatrix();

        // Convert to VkTransformMatrixKHR (row-major 3x4)
        VkTransformMatrixKHR transformMatrix;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                transformMatrix.matrix[row][col] = modelMatrix[col][row];
            }
        }

        VkAccelerationStructureInstanceKHR asInstance{};
        asInstance.transform = transformMatrix;
        asInstance.instanceCustomIndex = instanceIndex;  // Custom index for shader
        asInstance.mask = 0xFF;  // Visible to all rays
        asInstance.instanceShaderBindingTableRecordOffset = 0;  // Use first hit group
        asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        asInstance.accelerationStructureReference = blas->deviceAddress;

        tlasInstances.push_back(asInstance);
        instanceIndex++;
    }

    // Build or update TLAS
    if (!tlasInstances.empty()) {
        if (m_TLASDirty || !m_TLAS.isBuilt) {
            // Update geometry buffers before building TLAS
            updateGeometryBuffers();
            buildTLAS(tlasInstances);
        } else {
            updateTLAS(tlasInstances);
        }
    }
}

void RayTracingSystem::updateWorld(MiWorld* world) {
    if (!m_Initialized || !world) {
        return;
    }

    // Get all actors from the world
    const auto& actors = world->getAllActors();
    if (actors.empty()) {
        return;
    }

    // Track unique meshes
    std::unordered_map<uintptr_t, uint32_t> meshToId;
    uint32_t nextMeshId = 0;

    // Collect mesh instances from actors
    struct MeshInstanceInfo {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 transform;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
    };
    std::vector<MeshInstanceInfo> meshInstances;

    // Iterate actors and find static mesh components
    for (const auto& actorPtr : actors) {
        if (!actorPtr) continue;

        // Check if actor has a static mesh component
        auto meshComp = actorPtr->getComponent<MiStaticMeshComponent>();
        if (meshComp && meshComp->getMesh()) {
            MeshInstanceInfo info;
            info.mesh = meshComp->getMesh();
            info.transform = actorPtr->getTransform().getMatrix();

            // Get material properties from the component (per-instance material)
            const Material& mat = meshComp->getMaterial();
            info.baseColor = mat.diffuseColor;
            info.metallic = mat.metallic;
            info.roughness = mat.roughness;

            meshInstances.push_back(info);
        }
    }

    if (meshInstances.empty()) {
        return;
    }

    // First pass: create BLAS for unique meshes
    for (const auto& instance : meshInstances) {
        uintptr_t meshPtr = reinterpret_cast<uintptr_t>(instance.mesh.get());
        if (meshToId.find(meshPtr) == meshToId.end()) {
            meshToId[meshPtr] = nextMeshId;
            if (m_BLASMap.find(nextMeshId) == m_BLASMap.end()) {
                createBLAS(instance.mesh, nextMeshId);
            }
            nextMeshId++;
        }
    }

    // Second pass: build TLAS instances and update material buffer
    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    tlasInstances.reserve(meshInstances.size());

    // Prepare material data
    RTMaterialData* materials = nullptr;
    if (m_MaterialBufferMapped) {
        materials = static_cast<RTMaterialData*>(m_MaterialBufferMapped);
    }

    uint32_t instanceIndex = 0;
    for (const auto& instance : meshInstances) {
        uintptr_t meshPtr = reinterpret_cast<uintptr_t>(instance.mesh.get());
        auto meshIdIt = meshToId.find(meshPtr);
        if (meshIdIt == meshToId.end()) continue;

        const BLASInfo* blas = getBLAS(meshIdIt->second);
        if (!blas || !blas->isBuilt) continue;

        VkTransformMatrixKHR transformMatrix;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                transformMatrix.matrix[row][col] = instance.transform[col][row];
            }
        }

        VkAccelerationStructureInstanceKHR asInstance{};
        asInstance.transform = transformMatrix;
        asInstance.instanceCustomIndex = instanceIndex;  // This is used as material index in shader
        asInstance.mask = 0xFF;
        asInstance.instanceShaderBindingTableRecordOffset = 0;
        asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        asInstance.accelerationStructureReference = blas->deviceAddress;

        tlasInstances.push_back(asInstance);

        // Update material buffer for this instance
        if (materials && instanceIndex < RT_MAX_INSTANCES) {
            materials[instanceIndex].baseColor = glm::vec4(instance.baseColor, 1.0f);
            materials[instanceIndex].metallic = instance.metallic;
            materials[instanceIndex].roughness = instance.roughness;
            materials[instanceIndex].ao = 1.0f;
            materials[instanceIndex].emissive = 0.0f;
            materials[instanceIndex].albedoTexIndex = -1;
            materials[instanceIndex].normalTexIndex = -1;
            materials[instanceIndex].metallicRoughnessTexIndex = -1;
            materials[instanceIndex].emissiveTexIndex = -1;
            materials[instanceIndex].meshId = meshIdIt->second;  // Store mesh ID for geometry lookup
        }

        instanceIndex++;
    }

    m_MaterialCount = instanceIndex;

    // Build or update TLAS
    if (!tlasInstances.empty()) {
        if (m_TLASDirty || !m_TLAS.isBuilt) {
            // Update geometry buffers before building TLAS
            updateGeometryBuffers();
            buildTLAS(tlasInstances);
        } else {
            updateTLAS(tlasInstances);
        }
    }
}

// ============================================================================
// Denoiser Implementation
// ============================================================================

bool RayTracingSystem::createHistoryBuffers() {
    VkDevice device = m_Renderer->getDevice();

    // History buffers for temporal accumulation (same size as RT output)
    // History Reflection (RGBA16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    m_HistoryReflectionImage, m_HistoryReflectionMemory)) {
        std::cerr << "Failed to create history reflection image" << std::endl;
        return false;
    }
    m_HistoryReflectionImageView = createImageView(m_HistoryReflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // History Shadow (R16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    m_HistoryShadowImage, m_HistoryShadowMemory)) {
        std::cerr << "Failed to create history shadow image" << std::endl;
        return false;
    }
    m_HistoryShadowImageView = createImageView(m_HistoryShadowImage, VK_FORMAT_R16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Denoised output buffers (final result)
    // Denoised Reflection (RGBA16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    m_DenoisedReflectionImage, m_DenoisedReflectionMemory)) {
        std::cerr << "Failed to create denoised reflection image" << std::endl;
        return false;
    }
    m_DenoisedReflectionImageView = createImageView(m_DenoisedReflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Denoised Shadow (R16F)
    if (!createImage(m_OutputWidth, m_OutputHeight, VK_FORMAT_R16_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    m_DenoisedShadowImage, m_DenoisedShadowMemory)) {
        std::cerr << "Failed to create denoised shadow image" << std::endl;
        return false;
    }
    m_DenoisedShadowImageView = createImageView(m_DenoisedShadowImage, VK_FORMAT_R16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Transition all history/denoised images to general layout
    VkCommandBuffer cmdBuffer = m_Renderer->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkImage images[] = { m_HistoryReflectionImage, m_HistoryShadowImage,
                         m_DenoisedReflectionImage, m_DenoisedShadowImage };

    for (VkImage img : images) {
        barrier.image = img;
        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    m_Renderer->endSingleTimeCommands(cmdBuffer);

    // Create uniform buffers for denoiser settings
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();
    VkDeviceSize temporalBufferSize = sizeof(TemporalDenoiseUniforms);
    // Spatial uses same struct layout as temporal (shader reinterprets last 4 fields)
    VkDeviceSize spatialBufferSize = sizeof(TemporalDenoiseUniforms);

    m_DenoiseUniformBuffers.resize(framesInFlight * 2); // temporal + spatial per frame
    m_DenoiseUniformBuffersMemory.resize(framesInFlight * 2);
    m_DenoiseUniformBuffersMapped.resize(framesInFlight * 2);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Temporal uniform buffer
        createBuffer(temporalBufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_DenoiseUniformBuffers[i * 2], m_DenoiseUniformBuffersMemory[i * 2]);
        vkMapMemory(device, m_DenoiseUniformBuffersMemory[i * 2], 0, temporalBufferSize, 0, &m_DenoiseUniformBuffersMapped[i * 2]);

        // Spatial uniform buffer
        createBuffer(spatialBufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_DenoiseUniformBuffers[i * 2 + 1], m_DenoiseUniformBuffersMemory[i * 2 + 1]);
        vkMapMemory(device, m_DenoiseUniformBuffersMemory[i * 2 + 1], 0, spatialBufferSize, 0, &m_DenoiseUniformBuffersMapped[i * 2 + 1]);
    }

    std::cout << "Denoiser history buffers created (" << m_OutputWidth << "x" << m_OutputHeight << ")" << std::endl;
    return true;
}

bool RayTracingSystem::createDenoisePipelines() {
    VkDevice device = m_Renderer->getDevice();

    // Create descriptor set layout for denoiser
    // Temporal pass bindings:
    // 0: currentReflections (storage image, read)
    // 1: currentShadows (storage image, read)
    // 2: historyReflections (storage image, read/write)
    // 3: historyShadows (storage image, read/write)
    // 4: outputReflections (storage image, write)
    // 5: outputShadows (storage image, write)
    // 6: uniforms (uniform buffer)

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // currentReflections
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // currentShadows
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // historyReflections
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // historyShadows
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // outputReflections
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // outputShadows
        {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr} // uniforms
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DenoiseDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create denoise descriptor set layout" << std::endl;
        return false;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DenoiseDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_DenoisePipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create denoise pipeline layout" << std::endl;
        return false;
    }

    // Load temporal denoise shader
    auto temporalCode = readShaderFile("shaders/raytracing/denoise_temporal.comp.spv");
    VkShaderModule temporalModule = createShaderModule(temporalCode);

    VkComputePipelineCreateInfo temporalPipelineInfo{};
    temporalPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    temporalPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    temporalPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    temporalPipelineInfo.stage.module = temporalModule;
    temporalPipelineInfo.stage.pName = "main";
    temporalPipelineInfo.layout = m_DenoisePipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &temporalPipelineInfo, nullptr, &m_TemporalDenoisePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, temporalModule, nullptr);
        std::cerr << "Failed to create temporal denoise pipeline" << std::endl;
        return false;
    }
    vkDestroyShaderModule(device, temporalModule, nullptr);

    // Load spatial denoise shader
    auto spatialCode = readShaderFile("shaders/raytracing/denoise_spatial.comp.spv");
    VkShaderModule spatialModule = createShaderModule(spatialCode);

    VkComputePipelineCreateInfo spatialPipelineInfo{};
    spatialPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    spatialPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    spatialPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    spatialPipelineInfo.stage.module = spatialModule;
    spatialPipelineInfo.stage.pName = "main";
    spatialPipelineInfo.layout = m_DenoisePipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &spatialPipelineInfo, nullptr, &m_SpatialDenoisePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, spatialModule, nullptr);
        std::cerr << "Failed to create spatial denoise pipeline" << std::endl;
        return false;
    }
    vkDestroyShaderModule(device, spatialModule, nullptr);

    std::cout << "Denoise pipelines created" << std::endl;
    return true;
}

bool RayTracingSystem::createDenoiseDescriptorSets() {
    VkDevice device = m_Renderer->getDevice();
    uint32_t framesInFlight = m_Renderer->getMaxFramesInFlight();

    // Need 2 sets per frame (temporal + spatial), each with 6 images + 1 uniform
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, framesInFlight * 2 * 6}, // 6 storage images per set, 2 sets per frame
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight * 2}     // 1 uniform per set, 2 sets per frame
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight * 2; // temporal + spatial per frame

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DenoiseDescriptorPool) != VK_SUCCESS) {
        std::cerr << "Failed to create denoise descriptor pool" << std::endl;
        return false;
    }

    // Allocate temporal descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_DenoiseDescriptorSetLayout);
    m_DenoiseDescriptorSets.resize(framesInFlight);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DenoiseDescriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, m_DenoiseDescriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate temporal denoise descriptor sets" << std::endl;
        return false;
    }

    // Allocate spatial descriptor sets
    m_SpatialDenoiseDescriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, m_SpatialDenoiseDescriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate spatial denoise descriptor sets" << std::endl;
        return false;
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::vector<VkWriteDescriptorSet> writes;

        // Current RT outputs (binding 0, 1)
        VkDescriptorImageInfo currentReflInfo{};
        currentReflInfo.imageView = m_ReflectionImageView;
        currentReflInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet currentReflWrite{};
        currentReflWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        currentReflWrite.dstSet = m_DenoiseDescriptorSets[i];
        currentReflWrite.dstBinding = 0;
        currentReflWrite.dstArrayElement = 0;
        currentReflWrite.descriptorCount = 1;
        currentReflWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        currentReflWrite.pImageInfo = &currentReflInfo;
        writes.push_back(currentReflWrite);

        VkDescriptorImageInfo currentShadowInfo{};
        currentShadowInfo.imageView = m_ShadowImageView;
        currentShadowInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet currentShadowWrite{};
        currentShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        currentShadowWrite.dstSet = m_DenoiseDescriptorSets[i];
        currentShadowWrite.dstBinding = 1;
        currentShadowWrite.dstArrayElement = 0;
        currentShadowWrite.descriptorCount = 1;
        currentShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        currentShadowWrite.pImageInfo = &currentShadowInfo;
        writes.push_back(currentShadowWrite);

        // History buffers (binding 2, 3)
        VkDescriptorImageInfo historyReflInfo{};
        historyReflInfo.imageView = m_HistoryReflectionImageView;
        historyReflInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet historyReflWrite{};
        historyReflWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        historyReflWrite.dstSet = m_DenoiseDescriptorSets[i];
        historyReflWrite.dstBinding = 2;
        historyReflWrite.dstArrayElement = 0;
        historyReflWrite.descriptorCount = 1;
        historyReflWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        historyReflWrite.pImageInfo = &historyReflInfo;
        writes.push_back(historyReflWrite);

        VkDescriptorImageInfo historyShadowInfo{};
        historyShadowInfo.imageView = m_HistoryShadowImageView;
        historyShadowInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet historyShadowWrite{};
        historyShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        historyShadowWrite.dstSet = m_DenoiseDescriptorSets[i];
        historyShadowWrite.dstBinding = 3;
        historyShadowWrite.dstArrayElement = 0;
        historyShadowWrite.descriptorCount = 1;
        historyShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        historyShadowWrite.pImageInfo = &historyShadowInfo;
        writes.push_back(historyShadowWrite);

        // Output buffers (binding 4, 5)
        VkDescriptorImageInfo outReflInfo{};
        outReflInfo.imageView = m_DenoisedReflectionImageView;
        outReflInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet outReflWrite{};
        outReflWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        outReflWrite.dstSet = m_DenoiseDescriptorSets[i];
        outReflWrite.dstBinding = 4;
        outReflWrite.dstArrayElement = 0;
        outReflWrite.descriptorCount = 1;
        outReflWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outReflWrite.pImageInfo = &outReflInfo;
        writes.push_back(outReflWrite);

        VkDescriptorImageInfo outShadowInfo{};
        outShadowInfo.imageView = m_DenoisedShadowImageView;
        outShadowInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet outShadowWrite{};
        outShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        outShadowWrite.dstSet = m_DenoiseDescriptorSets[i];
        outShadowWrite.dstBinding = 5;
        outShadowWrite.dstArrayElement = 0;
        outShadowWrite.descriptorCount = 1;
        outShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outShadowWrite.pImageInfo = &outShadowInfo;
        writes.push_back(outShadowWrite);

        // Uniform buffer (binding 6) - temporal uniform buffer
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = m_DenoiseUniformBuffers[i * 2]; // Temporal uniform buffer
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(TemporalDenoiseUniforms);

        VkWriteDescriptorSet uniformWrite{};
        uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWrite.dstSet = m_DenoiseDescriptorSets[i];
        uniformWrite.dstBinding = 6;
        uniformWrite.dstArrayElement = 0;
        uniformWrite.descriptorCount = 1;
        uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.pBufferInfo = &uniformInfo;
        writes.push_back(uniformWrite);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // =============================================
        // Update spatial descriptor sets (same images, different uniform buffer)
        // =============================================
        std::vector<VkWriteDescriptorSet> spatialWrites;

        // Same image bindings (0-5), just with spatial descriptor set
        currentReflWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(currentReflWrite);

        currentShadowWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(currentShadowWrite);

        historyReflWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(historyReflWrite);

        historyShadowWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(historyShadowWrite);

        outReflWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(outReflWrite);

        outShadowWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialWrites.push_back(outShadowWrite);

        // Spatial uniform buffer (binding 6) - uses same layout as temporal
        VkDescriptorBufferInfo spatialUniformInfo{};
        spatialUniformInfo.buffer = m_DenoiseUniformBuffers[i * 2 + 1]; // Spatial uniform buffer
        spatialUniformInfo.offset = 0;
        spatialUniformInfo.range = sizeof(TemporalDenoiseUniforms); // Same layout, shader reinterprets fields

        VkWriteDescriptorSet spatialUniformWrite{};
        spatialUniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        spatialUniformWrite.dstSet = m_SpatialDenoiseDescriptorSets[i];
        spatialUniformWrite.dstBinding = 6;
        spatialUniformWrite.dstArrayElement = 0;
        spatialUniformWrite.descriptorCount = 1;
        spatialUniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        spatialUniformWrite.pBufferInfo = &spatialUniformInfo;
        spatialWrites.push_back(spatialUniformWrite);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(spatialWrites.size()), spatialWrites.data(), 0, nullptr);
    }

    std::cout << "Denoise descriptor sets created and updated" << std::endl;
    return true;
}

void RayTracingSystem::updateDenoiseDescriptorSets(uint32_t frameIndex) {
    // For temporal pass: use standard temporal uniforms
    // For spatial pass: we reuse the same struct but reinterpret the last 4 fields
    // The shader reads the last 4 fields (after cameraPos) as spatial settings

    // Temporal pass uniform buffer
    TemporalDenoiseUniforms temporalUniforms{};
    temporalUniforms.prevViewProj = m_PrevViewProj;
    temporalUniforms.currViewProjInv = glm::mat4(1.0f); // Would need to pass this in
    temporalUniforms.cameraPos = glm::vec4(0.0f); // Would need to pass this in
    temporalUniforms.temporalBlend = m_DenoiserSettings.temporalBlend;
    temporalUniforms.varianceClipGamma = m_DenoiserSettings.varianceClipGamma;
    temporalUniforms.frameNumber = static_cast<int32_t>(m_FrameNumber);
    temporalUniforms.enableTemporal = m_DenoiserSettings.enableTemporal ? 1 : 0;

    memcpy(m_DenoiseUniformBuffersMapped[frameIndex * 2], &temporalUniforms, sizeof(temporalUniforms));

    // Spatial pass reuses the same struct layout, but reinterprets the last 4 fields:
    // temporalBlend -> sigmaColor
    // varianceClipGamma -> sigmaSpatial
    // frameNumber -> kernelRadius
    // enableTemporal -> enabled
    TemporalDenoiseUniforms spatialUniforms{};
    spatialUniforms.prevViewProj = glm::mat4(1.0f);      // unused
    spatialUniforms.currViewProjInv = glm::mat4(1.0f);  // unused
    spatialUniforms.cameraPos = glm::vec4(0.0f);        // unused
    spatialUniforms.temporalBlend = m_DenoiserSettings.spatialColorSigma;    // -> sigmaColor
    spatialUniforms.varianceClipGamma = m_DenoiserSettings.spatialSigma;     // -> sigmaSpatial
    spatialUniforms.frameNumber = m_DenoiserSettings.spatialFilterRadius;    // -> kernelRadius
    spatialUniforms.enableTemporal = m_DenoiserSettings.enableSpatial ? 1 : 0; // -> enabled

    memcpy(m_DenoiseUniformBuffersMapped[frameIndex * 2 + 1], &spatialUniforms, sizeof(spatialUniforms));
}

void RayTracingSystem::cleanupDenoiser() {
    VkDevice device = m_Renderer->getDevice();

    // Cleanup pipelines
    if (m_TemporalDenoisePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_TemporalDenoisePipeline, nullptr);
        m_TemporalDenoisePipeline = VK_NULL_HANDLE;
    }
    if (m_SpatialDenoisePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_SpatialDenoisePipeline, nullptr);
        m_SpatialDenoisePipeline = VK_NULL_HANDLE;
    }
    if (m_DenoisePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_DenoisePipelineLayout, nullptr);
        m_DenoisePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_DenoiseDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_DenoiseDescriptorPool, nullptr);
        m_DenoiseDescriptorPool = VK_NULL_HANDLE;
    }
    m_DenoiseDescriptorSets.clear();
    m_SpatialDenoiseDescriptorSets.clear();
    if (m_DenoiseDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_DenoiseDescriptorSetLayout, nullptr);
        m_DenoiseDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Cleanup history images
    if (m_HistoryReflectionImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_HistoryReflectionImageView, nullptr);
        m_HistoryReflectionImageView = VK_NULL_HANDLE;
    }
    if (m_HistoryReflectionImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_HistoryReflectionImage, nullptr);
        m_HistoryReflectionImage = VK_NULL_HANDLE;
    }
    if (m_HistoryReflectionMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_HistoryReflectionMemory, nullptr);
        m_HistoryReflectionMemory = VK_NULL_HANDLE;
    }

    if (m_HistoryShadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_HistoryShadowImageView, nullptr);
        m_HistoryShadowImageView = VK_NULL_HANDLE;
    }
    if (m_HistoryShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_HistoryShadowImage, nullptr);
        m_HistoryShadowImage = VK_NULL_HANDLE;
    }
    if (m_HistoryShadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_HistoryShadowMemory, nullptr);
        m_HistoryShadowMemory = VK_NULL_HANDLE;
    }

    // Cleanup denoised images
    if (m_DenoisedReflectionImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_DenoisedReflectionImageView, nullptr);
        m_DenoisedReflectionImageView = VK_NULL_HANDLE;
    }
    if (m_DenoisedReflectionImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_DenoisedReflectionImage, nullptr);
        m_DenoisedReflectionImage = VK_NULL_HANDLE;
    }
    if (m_DenoisedReflectionMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_DenoisedReflectionMemory, nullptr);
        m_DenoisedReflectionMemory = VK_NULL_HANDLE;
    }

    if (m_DenoisedShadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_DenoisedShadowImageView, nullptr);
        m_DenoisedShadowImageView = VK_NULL_HANDLE;
    }
    if (m_DenoisedShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_DenoisedShadowImage, nullptr);
        m_DenoisedShadowImage = VK_NULL_HANDLE;
    }
    if (m_DenoisedShadowMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_DenoisedShadowMemory, nullptr);
        m_DenoisedShadowMemory = VK_NULL_HANDLE;
    }

    // Cleanup uniform buffers
    for (size_t i = 0; i < m_DenoiseUniformBuffers.size(); i++) {
        if (m_DenoiseUniformBuffersMapped[i]) {
            vkUnmapMemory(device, m_DenoiseUniformBuffersMemory[i]);
        }
        if (m_DenoiseUniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_DenoiseUniformBuffers[i], nullptr);
        }
        if (m_DenoiseUniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_DenoiseUniformBuffersMemory[i], nullptr);
        }
    }
    m_DenoiseUniformBuffers.clear();
    m_DenoiseUniformBuffersMemory.clear();
    m_DenoiseUniformBuffersMapped.clear();
}

} // namespace MiEngine
