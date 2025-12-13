#include "virtualgeo/VirtualGeoRenderer.h"
#include "VulkanRenderer.h"
#include <iostream>
#include <cstring>
#include <array>
#include <cmath>

namespace MiEngine {

VirtualGeoRenderer::VirtualGeoRenderer() {}

VirtualGeoRenderer::~VirtualGeoRenderer() {
    cleanup();
}

bool VirtualGeoRenderer::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    m_device = renderer->getDevice();

    std::cout << "[VirtualGeo] Initializing Virtual Geometry Renderer..." << std::endl;

    // Create descriptor pool
    if (!createDescriptorSets()) {
        std::cerr << "[VirtualGeo] Failed to create descriptor pool" << std::endl;
        return false;
    }

    // Create global buffers
    if (!createIndirectBuffer(MAX_DRAWS)) {
        std::cerr << "[VirtualGeo] Failed to create indirect buffer" << std::endl;
        return false;
    }

    if (!createClusterVisibilityBuffer(MAX_CLUSTERS)) {
        std::cerr << "[VirtualGeo] Failed to create visibility buffer" << std::endl;
        return false;
    }

    // Create instance buffer
    if (!createInstanceBuffer()) {
        std::cerr << "[VirtualGeo] Failed to create instance buffer" << std::endl;
        return false;
    }

    // Create culling uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(GPUCullingUniforms);
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_cullingUniformBuffer,
            m_cullingUniformMemory
        );
    }

    // Create compute pipeline for culling
    if (!createCullingPipeline()) {
        std::cerr << "[VirtualGeo] Failed to create culling pipeline" << std::endl;
        return false;
    }

    // Create rendering uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(VGRenderUniforms);
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_renderUniformBuffer,
            m_renderUniformMemory
        );
    }

    // Create rendering pipeline
    if (!createRenderingPipeline()) {
        std::cerr << "[VirtualGeo] Failed to create rendering pipeline" << std::endl;
        return false;
    }

    // Create per-frame resources for GPU-driven mode
    if (!createPerFrameResources()) {
        std::cerr << "[VirtualGeo] Failed to create per-frame resources" << std::endl;
        return false;
    }

    // Create Hi-Z occlusion culling resources
    VkExtent2D extent = m_renderer->getSwapChainExtent();
    if (!createHiZResources(extent.width, extent.height)) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z resources" << std::endl;
        // Non-fatal - occlusion culling will be disabled
    }

    if (!createHiZPipeline()) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z pipeline" << std::endl;
        // Non-fatal - occlusion culling will be disabled
    }

    if (!createHiZDebugPipeline()) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z debug pipeline" << std::endl;
        // Non-fatal - debug visualization will be disabled
    }

    if (!createHiZCopyPipeline()) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy pipeline" << std::endl;
        // Non-fatal - will fall back to compute shader (may not work on some GPUs)
    }

    // Allocate render descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_renderDescSetLayout;

        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_renderDescSet) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to allocate render descriptor set" << std::endl;
            return false;
        }

        // Update descriptor set - binding 0: uniform buffer, binding 1: instance buffer
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        // Binding 0: Uniform buffer
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = m_renderUniformBuffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(VGRenderUniforms);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_renderDescSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboInfo;

        // Binding 1: Instance buffer (transforms)
        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = m_instanceBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = sizeof(GPUInstanceData) * MAX_INSTANCES;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_renderDescSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &instanceInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }

    m_initialized = true;

    std::cout << "[VirtualGeo] Initialization complete" << std::endl;
    std::cout << "  Max clusters: " << MAX_CLUSTERS << std::endl;
    std::cout << "  Max instances: " << MAX_INSTANCES << std::endl;
    std::cout << "  Max draws: " << MAX_DRAWS << std::endl;

    return true;
}

void VirtualGeoRenderer::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_device);

    // Cleanup per-frame resources
    cleanupPerFrameResources();

    // Cleanup merged buffers
    cleanupMergedBuffers();

    // Cleanup Hi-Z resources
    cleanupHiZResources();

    // Cleanup meshes
    for (auto& [id, mesh] : m_meshes) {
        if (mesh.vertexBuffer) vkDestroyBuffer(m_device, mesh.vertexBuffer, nullptr);
        if (mesh.indexBuffer) vkDestroyBuffer(m_device, mesh.indexBuffer, nullptr);
        if (mesh.clusterBuffer) vkDestroyBuffer(m_device, mesh.clusterBuffer, nullptr);
        if (mesh.vertexMemory) vkFreeMemory(m_device, mesh.vertexMemory, nullptr);
        if (mesh.indexMemory) vkFreeMemory(m_device, mesh.indexMemory, nullptr);
        if (mesh.clusterMemory) vkFreeMemory(m_device, mesh.clusterMemory, nullptr);
    }
    m_meshes.clear();

    // Cleanup global buffers
    if (m_indirectBuffer) vkDestroyBuffer(m_device, m_indirectBuffer, nullptr);
    if (m_visibleClusterBuffer) vkDestroyBuffer(m_device, m_visibleClusterBuffer, nullptr);
    if (m_instanceBuffer) vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
    if (m_cullingUniformBuffer) vkDestroyBuffer(m_device, m_cullingUniformBuffer, nullptr);
    if (m_drawCountBuffer) vkDestroyBuffer(m_device, m_drawCountBuffer, nullptr);

    if (m_indirectMemory) vkFreeMemory(m_device, m_indirectMemory, nullptr);
    if (m_visibleClusterMemory) vkFreeMemory(m_device, m_visibleClusterMemory, nullptr);
    if (m_instanceMemory) vkFreeMemory(m_device, m_instanceMemory, nullptr);
    if (m_cullingUniformMemory) vkFreeMemory(m_device, m_cullingUniformMemory, nullptr);
    if (m_drawCountMemory) vkFreeMemory(m_device, m_drawCountMemory, nullptr);
    if (m_renderUniformBuffer) vkDestroyBuffer(m_device, m_renderUniformBuffer, nullptr);
    if (m_renderUniformMemory) vkFreeMemory(m_device, m_renderUniformMemory, nullptr);

    // Cleanup pipelines
    if (m_cullingPipeline) vkDestroyPipeline(m_device, m_cullingPipeline, nullptr);
    if (m_cullingPipelineLayout) vkDestroyPipelineLayout(m_device, m_cullingPipelineLayout, nullptr);
    if (m_renderPipeline) vkDestroyPipeline(m_device, m_renderPipeline, nullptr);
    if (m_renderPipelineLayout) vkDestroyPipelineLayout(m_device, m_renderPipelineLayout, nullptr);

    // Cleanup descriptor set layouts
    if (m_cullingDescSetLayout) vkDestroyDescriptorSetLayout(m_device, m_cullingDescSetLayout, nullptr);
    if (m_renderDescSetLayout) vkDestroyDescriptorSetLayout(m_device, m_renderDescSetLayout, nullptr);

    // Cleanup descriptor pool
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    m_initialized = false;
    m_device = VK_NULL_HANDLE;
}

bool VirtualGeoRenderer::createIndirectBuffer(uint32_t maxDraws) {
    VkDeviceSize bufferSize = sizeof(GPUDrawCommand) * maxDraws;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_indirectBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_indirectBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_indirectMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindBufferMemory(m_device, m_indirectBuffer, m_indirectMemory, 0);

    // Create draw count buffer (single uint32_t for atomic counter)
    VkBufferCreateInfo countBufferInfo{};
    countBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    countBufferInfo.size = sizeof(uint32_t);
    countBufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    countBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &countBufferInfo, nullptr, &m_drawCountBuffer) != VK_SUCCESS) {
        return false;
    }

    vkGetBufferMemoryRequirements(m_device, m_drawCountBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_drawCountMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindBufferMemory(m_device, m_drawCountBuffer, m_drawCountMemory, 0);

    std::cout << "[VirtualGeo] Created indirect buffer: " << bufferSize / 1024 << " KB" << std::endl;
    return true;
}

bool VirtualGeoRenderer::createClusterVisibilityBuffer(uint32_t maxClusters) {
    // Visibility buffer stores indices of visible clusters
    VkDeviceSize bufferSize = sizeof(uint32_t) * maxClusters;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_visibleClusterBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_visibleClusterBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_visibleClusterMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindBufferMemory(m_device, m_visibleClusterBuffer, m_visibleClusterMemory, 0);

    std::cout << "[VirtualGeo] Created visibility buffer: " << bufferSize / 1024 << " KB" << std::endl;
    return true;
}

uint32_t VirtualGeoRenderer::uploadClusteredMesh(const ClusteredMesh& mesh) {
    uint32_t meshId = m_nextMeshId++;
    ClusteredMeshGPU gpuMesh;
    gpuMesh.meshId = meshId;

    std::cout << "[VirtualGeo] Uploading mesh " << meshId << "..." << std::endl;
    std::cout << "  Vertices: " << mesh.vertices.size() << std::endl;
    std::cout << "  Indices: " << mesh.indices.size() << std::endl;
    std::cout << "  Clusters: " << mesh.clusters.size() << std::endl;

    // Cache source data for merged buffer rebuilding
    gpuMesh.sourceVertices = mesh.vertices;
    // Note: sourceClusters is set later after index reorganization (see below)

    // Create and upload vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(ClusterVertex) * mesh.vertices.size();
        gpuMesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

        // Create staging buffer
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        );

        // Copy data to staging
        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mesh.vertices.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        // Create device local buffer
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            gpuMesh.vertexBuffer,
            gpuMesh.vertexMemory
        );

        // Copy staging to device
        m_renderer->copyBuffer(stagingBuffer, gpuMesh.vertexBuffer, bufferSize);

        // Cleanup staging
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    // Create and upload index buffer
    // IMPORTANT: The indices in mesh.indices are LOCAL to each cluster (0, 1, 2, ...)
    // We need to convert them to GLOBAL indices by adding each cluster's vertexOffset
    // Also build per-LOD index ranges for selective LOD rendering
    {
        std::vector<uint32_t> globalIndices;
        globalIndices.reserve(mesh.indices.size());

        // Group clusters by LOD level and build index ranges
        gpuMesh.lodRanges.resize(mesh.maxLodLevel + 1);

        // First pass: count indices per LOD
        std::vector<uint32_t> lodIndexCounts(mesh.maxLodLevel + 1, 0);
        for (const auto& cluster : mesh.clusters) {
            lodIndexCounts[cluster.lodLevel] += cluster.triangleCount * 3;
        }

        // Calculate starting indices for each LOD
        uint32_t currentOffset = 0;
        for (uint32_t lod = 0; lod <= mesh.maxLodLevel; lod++) {
            gpuMesh.lodRanges[lod].firstIndex = currentOffset;
            gpuMesh.lodRanges[lod].indexCount = 0;  // Will be filled during second pass
            gpuMesh.lodRanges[lod].clusterCount = 0;
            currentOffset += lodIndexCounts[lod];
        }

        // Prepare per-LOD index lists and track cluster index offsets
        std::vector<std::vector<uint32_t>> lodIndices(mesh.maxLodLevel + 1);
        std::vector<uint32_t> lodCurrentOffset(mesh.maxLodLevel + 1, 0);  // Current write position per LOD
        for (uint32_t lod = 0; lod <= mesh.maxLodLevel; lod++) {
            lodIndices[lod].reserve(lodIndexCounts[lod]);
        }

        // Copy clusters and update their indexOffset to match the new layout
        gpuMesh.sourceClusters = mesh.clusters;  // Make a copy we can modify

        // Second pass: fill per-LOD index lists with global indices
        // AND update cluster indexOffset to point to new location
        for (size_t ci = 0; ci < mesh.clusters.size(); ci++) {
            const auto& cluster = mesh.clusters[ci];
            uint32_t lod = cluster.lodLevel;

            // Record where this cluster's indices will be in the LOD-organized buffer
            uint32_t newIndexOffset = gpuMesh.lodRanges[lod].firstIndex + lodCurrentOffset[lod];
            gpuMesh.sourceClusters[ci].indexOffset = newIndexOffset;

            for (uint32_t i = 0; i < cluster.triangleCount * 3; i++) {
                uint32_t localIdx = mesh.indices[cluster.indexOffset + i];
                // Convert to global index by adding the cluster's vertex offset
                lodIndices[lod].push_back(localIdx + cluster.vertexOffset);
            }
            lodCurrentOffset[lod] += cluster.triangleCount * 3;
            gpuMesh.lodRanges[lod].clusterCount++;
        }

        // Combine all LOD indices into one buffer (LOD 0 first, then LOD 1, etc.)
        for (uint32_t lod = 0; lod <= mesh.maxLodLevel; lod++) {
            gpuMesh.lodRanges[lod].indexCount = static_cast<uint32_t>(lodIndices[lod].size());
            for (uint32_t idx : lodIndices[lod]) {
                globalIndices.push_back(idx);
            }
        }

        // Cache global indices for merged buffer rebuilding
        gpuMesh.sourceIndices = globalIndices;

        VkDeviceSize bufferSize = sizeof(uint32_t) * globalIndices.size();
        gpuMesh.indexCount = static_cast<uint32_t>(globalIndices.size());

        // Debug: verify cluster index coverage
        std::cout << "  Cluster index verification:" << std::endl;
        uint32_t totalIndexesFromClusters = 0;
        for (const auto& c : gpuMesh.sourceClusters) {
            totalIndexesFromClusters += c.triangleCount * 3;
        }
        std::cout << "    Total indices from clusters: " << totalIndexesFromClusters << std::endl;
        std::cout << "    Total indices in buffer: " << globalIndices.size() << std::endl;
        if (totalIndexesFromClusters != globalIndices.size()) {
            std::cout << "    WARNING: Index count mismatch!" << std::endl;
        }

        std::cout << "  Per-LOD index ranges:" << std::endl;
        for (uint32_t lod = 0; lod <= mesh.maxLodLevel; lod++) {
            std::cout << "    LOD " << lod << ": firstIndex=" << gpuMesh.lodRanges[lod].firstIndex
                      << ", indexCount=" << gpuMesh.lodRanges[lod].indexCount
                      << ", clusters=" << gpuMesh.lodRanges[lod].clusterCount << std::endl;
        }

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        );

        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, globalIndices.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            gpuMesh.indexBuffer,
            gpuMesh.indexMemory
        );

        m_renderer->copyBuffer(stagingBuffer, gpuMesh.indexBuffer, bufferSize);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    // Create and upload cluster data buffer
    {
        std::vector<GPUClusterData> gpuClusters;
        gpuClusters.reserve(mesh.clusters.size());

        for (size_t i = 0; i < mesh.clusters.size(); ++i) {
            const Cluster& cluster = mesh.clusters[i];
            GPUClusterData gpuCluster;
            gpuCluster.boundingSphere = glm::vec4(
                cluster.boundingSphereCenter,
                cluster.boundingSphereRadius
            );
            gpuCluster.aabbMin = glm::vec4(cluster.aabbMin, cluster.lodError);
            gpuCluster.aabbMax = glm::vec4(cluster.aabbMax, cluster.parentError);
            gpuCluster.vertexOffset = cluster.vertexOffset;
            gpuCluster.vertexCount = cluster.vertexCount;
            gpuCluster.indexOffset = cluster.indexOffset;
            gpuCluster.triangleCount = cluster.triangleCount;
            gpuCluster.lodLevel = cluster.lodLevel;
            gpuCluster.materialIndex = cluster.materialIndex;
            gpuCluster.flags = cluster.flags;
            gpuCluster.padding = 0;

            gpuClusters.push_back(gpuCluster);
        }

        VkDeviceSize bufferSize = sizeof(GPUClusterData) * gpuClusters.size();
        gpuMesh.clusterCount = static_cast<uint32_t>(gpuClusters.size());

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        );

        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, gpuClusters.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            gpuMesh.clusterBuffer,
            gpuMesh.clusterMemory
        );

        m_renderer->copyBuffer(stagingBuffer, gpuMesh.clusterBuffer, bufferSize);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    gpuMesh.maxLodLevel = mesh.maxLodLevel;
    gpuMesh.aabbMin = mesh.aabbMin;
    gpuMesh.aabbMax = mesh.aabbMax;

    m_meshes[meshId] = std::move(gpuMesh);
    m_totalClusterCount += m_meshes[meshId].clusterCount;

    // Mark merged buffers as needing rebuild
    m_mergedData.dirty = true;

    std::cout << "[VirtualGeo] Mesh " << meshId << " uploaded successfully" << std::endl;
    return meshId;
}

void VirtualGeoRenderer::removeClusteredMesh(uint32_t meshId) {
    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return;

    vkDeviceWaitIdle(m_device);

    ClusteredMeshGPU& mesh = it->second;
    m_totalClusterCount -= mesh.clusterCount;

    if (mesh.vertexBuffer) vkDestroyBuffer(m_device, mesh.vertexBuffer, nullptr);
    if (mesh.indexBuffer) vkDestroyBuffer(m_device, mesh.indexBuffer, nullptr);
    if (mesh.clusterBuffer) vkDestroyBuffer(m_device, mesh.clusterBuffer, nullptr);
    if (mesh.vertexMemory) vkFreeMemory(m_device, mesh.vertexMemory, nullptr);
    if (mesh.indexMemory) vkFreeMemory(m_device, mesh.indexMemory, nullptr);
    if (mesh.clusterMemory) vkFreeMemory(m_device, mesh.clusterMemory, nullptr);

    m_meshes.erase(it);
}

uint32_t VirtualGeoRenderer::addInstance(uint32_t meshId, const glm::mat4& transform) {
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        std::cerr << "[VirtualGeo] Cannot add instance: mesh " << meshId << " not found" << std::endl;
        return 0;
    }

    uint32_t instanceId = m_nextInstanceId++;

    GPUInstanceData instance;
    instance.modelMatrix = transform;
    instance.normalMatrix = glm::transpose(glm::inverse(transform));
    instance.clusterOffset = 0;  // Will be computed during culling
    instance.clusterCount = meshIt->second.clusterCount;
    instance.pad0 = meshId;  // Store mesh ID for lookup
    instance.pad1 = 0;

    m_instances[instanceId] = instance;

    std::cout << "[VirtualGeo] Added instance " << instanceId << " of mesh " << meshId << std::endl;
    return instanceId;
}

void VirtualGeoRenderer::updateInstance(uint32_t instanceId, const glm::mat4& transform) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;

    it->second.modelMatrix = transform;
    it->second.normalMatrix = glm::transpose(glm::inverse(transform));
}

void VirtualGeoRenderer::removeInstance(uint32_t instanceId) {
    m_instances.erase(instanceId);
}

void VirtualGeoRenderer::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Extract frustum planes from view-projection matrix
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );
    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
}

void VirtualGeoRenderer::beginFrame(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
    m_viewMatrix = view;
    m_projMatrix = projection;
    m_cameraPosition = cameraPos;

    // Read back visible cluster count from frame 0 for now
    // (Frame 1's descriptor set binding issue still being debugged)
    if (m_gpuDrivenEnabled) {
        auto& fr = m_frameResources[0];
        if (fr.drawCountMemory != VK_NULL_HANDLE) {
            uint32_t* countPtr = nullptr;
            if (vkMapMemory(m_device, fr.drawCountMemory, 0, sizeof(uint32_t), 0,
                    reinterpret_cast<void**>(&countPtr)) == VK_SUCCESS) {
                m_visibleClusterCount = *countPtr;
                vkUnmapMemory(m_device, fr.drawCountMemory);
            }
        }
    }

    // Advance frame index for per-frame resources
    uint32_t oldFrame = m_currentFrame;
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // Rebuild merged buffers if dirty (GPU-driven mode)
    if (m_gpuDrivenEnabled && m_mergedData.dirty) {
        rebuildMergedBuffers();
    }

    // Update culling uniforms
    glm::mat4 viewProj = projection * view;
    m_cullingUniforms.viewProjection = viewProj;
    m_cullingUniforms.view = view;
    extractFrustumPlanes(viewProj, m_cullingUniforms.frustumPlanes);
    m_cullingUniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    m_cullingUniforms.screenParams = glm::vec4(
        static_cast<float>(m_renderer->getSwapChainExtent().width),
        static_cast<float>(m_renderer->getSwapChainExtent().height),
        0.1f,   // Near plane
        1000.0f // Far plane
    );
    m_cullingUniforms.lodBias = m_lodBias;
    m_cullingUniforms.errorThreshold = m_errorThreshold;
    m_cullingUniforms.totalClusters = m_totalClusterCount;
    m_cullingUniforms.frameIndex++;
    m_cullingUniforms.forcedLodLevel = m_forcedLodLevel;
    // Use forced LOD when auto LOD selection is disabled
    m_cullingUniforms.useForcedLod = m_lodSelectionEnabled ? 0 : 1;
    m_cullingUniforms.enableFrustumCulling = m_frustumCullingEnabled ? 1 : 0;
    // Disable occlusion culling on first frame (Hi-Z not built yet)
    m_cullingUniforms.enableOcclusionCulling = (m_occlusionCullingEnabled && !m_firstFrame) ? 1 : 0;
    // Hi-Z occlusion parameters
    m_cullingUniforms.hizMaxMipLevel = m_hizMaxMipLevel;
    m_cullingUniforms.hizDepthBias = m_hizDepthBias;
    m_cullingUniforms.hizDepthThreshold = m_hizDepthThreshold;
    m_cullingUniforms.hizPadding = 0.0f;

    // Reset draw call count (visible cluster count is preserved from readback above)
    m_drawCallCount = 0;
}

void VirtualGeoRenderer::dispatchCulling(VkCommandBuffer cmd) {
    if (m_meshes.empty() || m_instances.empty()) {
        return;
    }

    if (!m_gpuDrivenEnabled) {
        return;
    }

    if (m_mergedData.clusterBuffer == VK_NULL_HANDLE) {
        return;
    }

    // Use current frame index (set by beginFrame) for proper double-buffering
    // This ensures Hi-Z reads from previous frame while compute writes to current frame

    // Debug: Log first 10 consecutive frames to verify alternation
    static uint32_t debugCounter = 0;
    if (debugCounter < 10) {
        std::cout << "[VirtualGeo] dispatchCulling frame " << debugCounter
                  << ", m_currentFrame=" << m_currentFrame << std::endl;
        debugCounter++;
    }

    // Update instance data on GPU
    uploadInstanceData();

    // Update culling uniforms
    updateCullingUniforms();

    // Get per-frame resources
    auto& frame = m_frameResources[m_currentFrame];

    // Reset draw count to 0 using a buffer fill command
    vkCmdFillBuffer(cmd, frame.drawCountBuffer, 0, sizeof(uint32_t), 0);

    // Memory barrier to ensure fill is complete before compute
    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    // Bind compute pipeline with per-frame descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullingPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullingPipelineLayout,
        0, 1, &frame.cullingDescSet, 0, nullptr);

    // Dispatch compute shader for each instance using merged cluster buffer
    uint32_t instanceIndex = 0;

    for (const auto& [meshId, mesh] : m_meshes) {
        // Find instances of this mesh
        for (const auto& [instId, instData] : m_instances) {
            if (instData.pad0 == meshId) {  // pad0 stores mesh ID
                // Push constants for this dispatch
                struct {
                    uint32_t instanceIndex;
                    uint32_t clusterStartIndex;
                    uint32_t clusterCount;
                    uint32_t maxLodLevel;
                } pushConstants;

                pushConstants.instanceIndex = instanceIndex;
                pushConstants.clusterStartIndex = mesh.globalClusterOffset;  // Use global offset
                pushConstants.clusterCount = mesh.clusterCount;
                pushConstants.maxLodLevel = mesh.maxLodLevel;

                vkCmdPushConstants(cmd, m_cullingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                    0, sizeof(pushConstants), &pushConstants);

                // Dispatch - 64 threads per workgroup
                uint32_t workgroupCount = (mesh.clusterCount + 63) / 64;
                vkCmdDispatch(cmd, workgroupCount, 1, 1);

                instanceIndex++;
            }
        }
    }

    // Memory barrier: compute shader writes -> graphics reads
    VkMemoryBarrier computeBarrier{};
    computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

void VirtualGeoRenderer::draw(VkCommandBuffer cmd) {
    if (m_meshes.empty() || m_instances.empty()) {
        return;
    }

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_renderer->getSwapChainExtent().width);
    viewport.height = static_cast<float>(m_renderer->getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_renderer->getSwapChainExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind rendering pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipeline);

    // Update shared uniforms once (view, projection, lighting - same for all instances)
    m_renderUniforms.view = m_viewMatrix;
    // Apply Vulkan Y-flip to projection matrix
    glm::mat4 projFlipped = m_projMatrix;
    projFlipped[1][1] *= -1.0f;
    m_renderUniforms.projection = projFlipped;
    m_renderUniforms.cameraPosition = glm::vec4(m_cameraPosition, 1.0f);
    m_renderUniforms.lightDirection = glm::vec4(glm::normalize(m_lightDirection), m_lightIntensity);
    m_renderUniforms.lightColor = glm::vec4(m_lightColor, m_ambientIntensity);

    // Upload shared uniforms once
    void* data;
    vkMapMemory(m_device, m_renderUniformMemory, 0, sizeof(VGRenderUniforms), 0, &data);
    memcpy(data, &m_renderUniforms, sizeof(VGRenderUniforms));
    vkUnmapMemory(m_device, m_renderUniformMemory);

    // Bind descriptor set (shared uniforms)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipelineLayout,
        0, 1, &m_renderDescSet, 0, nullptr);

    if (m_gpuDrivenEnabled && m_mergedData.vertexBuffer != VK_NULL_HANDLE) {
        // ========================================
        // GPU-DRIVEN RENDERING PATH
        // ========================================
        auto& frame = m_frameResources[m_currentFrame];

        // Upload instance data before drawing
        uploadInstanceData();

        // Bind merged vertex and index buffers
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_mergedData.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, m_mergedData.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Push constants - shader reads transforms from instance buffer
        VGPushConstants pushConstants;
        pushConstants.model = glm::mat4(1.0f);  // Not used in GPU-driven mode
        pushConstants.debugMode = m_debugMode;
        pushConstants.lodLevel = m_forcedLodLevel;
        pushConstants.clusterId = 0;
        pushConstants.useInstanceBuffer = 1;  // GPU-driven: use instance buffer

        vkCmdPushConstants(cmd, m_renderPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(VGPushConstants), &pushConstants);

        // Draw using indirect count - draw count is determined by compute shader
        vkCmdDrawIndexedIndirectCount(
            cmd,
            frame.indirectBuffer,           // Indirect buffer with draw commands
            0,                              // Offset into indirect buffer
            frame.drawCountBuffer,          // Buffer containing draw count
            0,                              // Offset into count buffer
            MAX_DRAWS,                      // Max draw count
            sizeof(GPUDrawCommand)          // Stride between draw commands
        );

        m_drawCallCount = 1;  // Single indirect draw call

        // Note: Visible cluster count will be read back in endFrame() after GPU completes
    } else {
        // ========================================
        // DIRECT RENDERING PATH (non-GPU-driven)
        // ========================================
        uint32_t instanceIdx = 0;
        for (const auto& [instId, instData] : m_instances) {
            uint32_t meshId = instData.pad0;  // mesh ID stored in pad0
            auto meshIt = m_meshes.find(meshId);
            if (meshIt == m_meshes.end()) continue;

            const ClusteredMeshGPU& mesh = meshIt->second;

            // Push constants for per-instance model matrix and debug info
            VGPushConstants pushConstants;
            pushConstants.model = instData.modelMatrix;
            pushConstants.debugMode = m_debugMode;
            pushConstants.lodLevel = m_forcedLodLevel;
            pushConstants.clusterId = instanceIdx;
            pushConstants.useInstanceBuffer = 0;  // Direct: use push constant model

            vkCmdPushConstants(cmd, m_renderPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(VGPushConstants), &pushConstants);

            // Bind vertex and index buffers
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // Draw only clusters at the selected LOD level
            if (mesh.lodRanges.size() > m_forcedLodLevel) {
                const auto& lodRange = mesh.lodRanges[m_forcedLodLevel];
                vkCmdDrawIndexed(cmd, lodRange.indexCount, 1, lodRange.firstIndex, 0, 0);
                m_visibleClusterCount += lodRange.clusterCount;
            } else if (!mesh.lodRanges.empty()) {
                // Fallback to LOD 0 if requested LOD doesn't exist
                const auto& lodRange = mesh.lodRanges[0];
                vkCmdDrawIndexed(cmd, lodRange.indexCount, 1, lodRange.firstIndex, 0, 0);
                m_visibleClusterCount += lodRange.clusterCount;
            }

            m_drawCallCount++;
            instanceIdx++;
        }
    }
}

void VirtualGeoRenderer::endFrame() {
    // Any per-frame cleanup
}

uint32_t VirtualGeoRenderer::getMaxLodLevel() const {
    uint32_t maxLod = 0;
    for (const auto& [id, mesh] : m_meshes) {
        maxLod = std::max(maxLod, mesh.maxLodLevel);
    }
    return maxLod;
}

bool VirtualGeoRenderer::createDescriptorSets() {
    // Create descriptor pool for culling shader and Hi-Z generation
    // Need enough for: 1 base culling set + MAX_FRAMES_IN_FLIGHT per-frame sets + 1 render set + Hi-Z mip sets
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 8;  // Increased for per-frame sets
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 40; // 6 per culling set * (1 + MAX_FRAMES_IN_FLIGHT)
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 16; // For Hi-Z mip generation (up to 16 mip levels)
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = 16; // For Hi-Z mip output (up to 16 mip levels)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 24;  // Increased for per-frame sets + Hi-Z mip sets

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create descriptor pool" << std::endl;
        return false;
    }

    // Create culling descriptor set layout
    // Binding 0: Culling uniforms (uniform buffer)
    // Binding 1: Cluster data (storage buffer, read-only)
    // Binding 2: Instance data (storage buffer, read-only)
    // Binding 3: Draw commands (storage buffer, read-write)
    // Binding 4: Draw count (storage buffer, read-write)
    // Binding 5: Visible clusters (storage buffer, read-write)
    // Binding 6: Hi-Z pyramid (combined image sampler)

    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

    // Binding 0: Culling uniforms
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Cluster data
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Instance data
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Draw commands output
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Draw count
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: Visible clusters
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: Hi-Z pyramid for occlusion culling
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_cullingDescSetLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create culling descriptor set layout" << std::endl;
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_cullingDescSetLayout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_cullingDescSet) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to allocate culling descriptor set" << std::endl;
        return false;
    }

    std::cout << "[VirtualGeo] Created descriptor pool, layout, and set" << std::endl;
    return true;
}

bool VirtualGeoRenderer::createCullingPipeline() {
    // Load compute shader
    auto shaderCode = m_renderer->readFile("shaders/virtualgeo/cluster_cull.comp.spv");
    if (shaderCode.empty()) {
        std::cerr << "[VirtualGeo] Failed to load cluster_cull.comp.spv" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = shaderCode.size();
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &shaderModuleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create shader module" << std::endl;
        return false;
    }

    // Push constant range for per-dispatch data
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(uint32_t) * 4;  // instanceIndex, clusterStartIndex, clusterCount, maxLodLevel

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_cullingDescSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_cullingPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        std::cerr << "[VirtualGeo] Failed to create culling pipeline layout" << std::endl;
        return false;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_cullingPipelineLayout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_cullingPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        std::cerr << "[VirtualGeo] Failed to create culling compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(m_device, shaderModule, nullptr);

    std::cout << "[VirtualGeo] Created culling compute pipeline" << std::endl;
    return true;
}

bool VirtualGeoRenderer::createPipelines() {
    // Create both culling and rendering pipelines
    if (!createCullingPipeline()) {
        return false;
    }
    // Rendering pipeline will be created when needed
    return true;
}

bool VirtualGeoRenderer::createRenderingPipeline() {
    // Load shaders
    auto vertShaderCode = m_renderer->readFile("shaders/virtualgeo/cluster.vert.spv");
    auto fragShaderCode = m_renderer->readFile("shaders/virtualgeo/cluster.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        std::cerr << "[VirtualGeo] Failed to load cluster shaders" << std::endl;
        return false;
    }

    VkShaderModule vertShaderModule = m_renderer->createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = m_renderer->createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input - ClusterVertex format (48 bytes)
    // position (vec3) at offset 0
    // normal (vec3) at offset 16 (after padding)
    // texCoord (vec2) at offset 32 (after padding)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(ClusterVertex);  // 48 bytes
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Position at offset 0
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(ClusterVertex, position);

    // Normal at offset 16 (position is 12 bytes + 4 padding)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(ClusterVertex, normal);

    // TexCoord at offset 32
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(ClusterVertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_renderer->getSwapChainExtent().width);
    viewport.height = static_cast<float>(m_renderer->getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_renderer->getSwapChainExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;  // Match PBR pipeline winding order
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic states for viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create descriptor set layout for rendering
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Binding 0: Uniform buffer (camera, lighting)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: Instance buffer (transforms for GPU-driven mode)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_renderDescSetLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create render descriptor set layout" << std::endl;
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }

    // Push constant range - includes model matrix + debug info (80 bytes)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VGPushConstants);  // mat4 model + debugMode, lodLevel, clusterId, pad = 80 bytes

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_renderDescSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_renderPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create render pipeline layout" << std::endl;
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }

    // Create graphics pipeline
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_renderPipelineLayout;
    pipelineInfo.renderPass = m_renderer->getRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_renderPipeline) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create render pipeline" << std::endl;
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);

    std::cout << "[VirtualGeo] Created rendering pipeline" << std::endl;
    return true;
}

void VirtualGeoRenderer::updateCullingUniforms() {
    // Upload culling uniforms to GPU
    void* data;
    vkMapMemory(m_device, m_cullingUniformMemory, 0, sizeof(GPUCullingUniforms), 0, &data);
    memcpy(data, &m_cullingUniforms, sizeof(GPUCullingUniforms));
    vkUnmapMemory(m_device, m_cullingUniformMemory);
}

bool VirtualGeoRenderer::createInstanceBuffer() {
    VkDeviceSize bufferSize = sizeof(GPUInstanceData) * MAX_INSTANCES;

    m_renderer->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_instanceBuffer,
        m_instanceMemory
    );

    std::cout << "[VirtualGeo] Created instance buffer: " << bufferSize / 1024 << " KB" << std::endl;
    return true;
}

void VirtualGeoRenderer::updateDescriptorSets(VkBuffer clusterBuffer, VkDeviceSize clusterBufferSize) {
    std::array<VkWriteDescriptorSet, 7> descriptorWrites{};

    // Binding 0: Culling uniforms
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = m_cullingUniformBuffer;
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(GPUCullingUniforms);

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_cullingDescSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &uniformBufferInfo;

    // Binding 1: Cluster data
    VkDescriptorBufferInfo clusterBufferInfo{};
    clusterBufferInfo.buffer = clusterBuffer;
    clusterBufferInfo.offset = 0;
    clusterBufferInfo.range = clusterBufferSize;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_cullingDescSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &clusterBufferInfo;

    // Binding 2: Instance data
    VkDescriptorBufferInfo instanceBufferInfo{};
    instanceBufferInfo.buffer = m_instanceBuffer;
    instanceBufferInfo.offset = 0;
    instanceBufferInfo.range = sizeof(GPUInstanceData) * MAX_INSTANCES;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_cullingDescSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &instanceBufferInfo;

    // Binding 3: Draw commands
    VkDescriptorBufferInfo drawBufferInfo{};
    drawBufferInfo.buffer = m_indirectBuffer;
    drawBufferInfo.offset = 0;
    drawBufferInfo.range = sizeof(GPUDrawCommand) * MAX_DRAWS;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = m_cullingDescSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &drawBufferInfo;

    // Binding 4: Draw count
    VkDescriptorBufferInfo countBufferInfo{};
    countBufferInfo.buffer = m_drawCountBuffer;
    countBufferInfo.offset = 0;
    countBufferInfo.range = sizeof(uint32_t);

    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = m_cullingDescSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pBufferInfo = &countBufferInfo;

    // Binding 5: Visible clusters
    VkDescriptorBufferInfo visibleBufferInfo{};
    visibleBufferInfo.buffer = m_visibleClusterBuffer;
    visibleBufferInfo.offset = 0;
    visibleBufferInfo.range = sizeof(uint32_t) * MAX_CLUSTERS;

    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = m_cullingDescSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pBufferInfo = &visibleBufferInfo;

    // Binding 6: Hi-Z pyramid (if available)
    VkDescriptorImageInfo hizImageInfo{};
    hizImageInfo.sampler = m_hizSampler;
    hizImageInfo.imageView = m_hizImageView;
    hizImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[6].dstSet = m_cullingDescSet;
    descriptorWrites[6].dstBinding = 6;
    descriptorWrites[6].dstArrayElement = 0;
    descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[6].descriptorCount = 1;
    descriptorWrites[6].pImageInfo = &hizImageInfo;

    // Only update Hi-Z binding if resources exist
    uint32_t writeCount = (m_hizImageView != VK_NULL_HANDLE && m_hizSampler != VK_NULL_HANDLE) ? 7 : 6;
    vkUpdateDescriptorSets(m_device, writeCount, descriptorWrites.data(), 0, nullptr);
}

void VirtualGeoRenderer::uploadInstanceData() {
    if (m_instances.empty()) return;

    // Build a packed array of instance data
    std::vector<GPUInstanceData> instanceData;
    instanceData.reserve(m_instances.size());

    uint32_t clusterOffset = 0;
    for (const auto& [id, instance] : m_instances) {
        GPUInstanceData gpuInstance = instance;
        gpuInstance.clusterOffset = clusterOffset;
        instanceData.push_back(gpuInstance);
        clusterOffset += instance.clusterCount;
    }

    // Upload to GPU
    VkDeviceSize dataSize = sizeof(GPUInstanceData) * instanceData.size();
    void* data;
    vkMapMemory(m_device, m_instanceMemory, 0, dataSize, 0, &data);
    memcpy(data, instanceData.data(), dataSize);
    vkUnmapMemory(m_device, m_instanceMemory);
}

bool VirtualGeoRenderer::createPerFrameResources() {
    std::cout << "[VirtualGeo] Creating per-frame resources for GPU-driven mode..." << std::endl;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto& frame = m_frameResources[i];

        // Create indirect buffer for this frame
        VkDeviceSize indirectSize = sizeof(GPUDrawCommand) * MAX_DRAWS;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indirectSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &frame.indirectBuffer) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to create per-frame indirect buffer " << i << std::endl;
            return false;
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device, frame.indirectBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
            memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &frame.indirectMemory) != VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(m_device, frame.indirectBuffer, frame.indirectMemory, 0);

        // Create draw count buffer
        VkBufferCreateInfo countInfo{};
        countInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        countInfo.size = sizeof(uint32_t);
        countInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        countInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &countInfo, nullptr, &frame.drawCountBuffer) != VK_SUCCESS) {
            return false;
        }

        vkGetBufferMemoryRequirements(m_device, frame.drawCountBuffer, &memReq);
        allocInfo.allocationSize = memReq.size;
        // Use host-visible memory so we can read back the draw count for statistics
        allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &frame.drawCountMemory) != VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(m_device, frame.drawCountBuffer, frame.drawCountMemory, 0);

        // Create visible cluster buffer
        VkDeviceSize visibleSize = sizeof(uint32_t) * MAX_CLUSTERS;
        VkBufferCreateInfo visibleInfo{};
        visibleInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        visibleInfo.size = visibleSize;
        visibleInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        visibleInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &visibleInfo, nullptr, &frame.visibleClusterBuffer) != VK_SUCCESS) {
            return false;
        }

        vkGetBufferMemoryRequirements(m_device, frame.visibleClusterBuffer, &memReq);
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
            memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &frame.visibleClusterMemory) != VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(m_device, frame.visibleClusterBuffer, frame.visibleClusterMemory, 0);

        // Allocate descriptor set for this frame
        VkDescriptorSetAllocateInfo descAllocInfo{};
        descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descAllocInfo.descriptorPool = m_descriptorPool;
        descAllocInfo.descriptorSetCount = 1;
        descAllocInfo.pSetLayouts = &m_cullingDescSetLayout;

        if (vkAllocateDescriptorSets(m_device, &descAllocInfo, &frame.cullingDescSet) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to allocate per-frame descriptor set " << i << std::endl;
            return false;
        }
    }

    std::cout << "[VirtualGeo] Per-frame resources created (" << MAX_FRAMES_IN_FLIGHT << " frames)" << std::endl;
    return true;
}

void VirtualGeoRenderer::cleanupPerFrameResources() {
    for (auto& frame : m_frameResources) {
        if (frame.indirectBuffer) vkDestroyBuffer(m_device, frame.indirectBuffer, nullptr);
        if (frame.drawCountBuffer) vkDestroyBuffer(m_device, frame.drawCountBuffer, nullptr);
        if (frame.visibleClusterBuffer) vkDestroyBuffer(m_device, frame.visibleClusterBuffer, nullptr);
        if (frame.indirectMemory) vkFreeMemory(m_device, frame.indirectMemory, nullptr);
        if (frame.drawCountMemory) vkFreeMemory(m_device, frame.drawCountMemory, nullptr);
        if (frame.visibleClusterMemory) vkFreeMemory(m_device, frame.visibleClusterMemory, nullptr);
        frame = PerFrameResources{};
    }
}

void VirtualGeoRenderer::updatePerFrameDescriptorSet(uint32_t frameIndex) {
    auto& frame = m_frameResources[frameIndex];
    if (m_mergedData.clusterBuffer == VK_NULL_HANDLE) return;

    std::array<VkWriteDescriptorSet, 7> writes{};

    // Binding 0: Culling uniforms
    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = m_cullingUniformBuffer;
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(GPUCullingUniforms);

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = frame.cullingDescSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uniformInfo;

    // Binding 1: Merged cluster data (GPUClusterDataExt)
    VkDescriptorBufferInfo clusterInfo{};
    clusterInfo.buffer = m_mergedData.clusterBuffer;
    clusterInfo.offset = 0;
    clusterInfo.range = sizeof(GPUClusterDataExt) * m_mergedData.totalClusters;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = frame.cullingDescSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &clusterInfo;

    // Binding 2: Instance data
    VkDescriptorBufferInfo instanceInfo{};
    instanceInfo.buffer = m_instanceBuffer;
    instanceInfo.offset = 0;
    instanceInfo.range = sizeof(GPUInstanceData) * MAX_INSTANCES;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = frame.cullingDescSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &instanceInfo;

    // Binding 3: Draw commands (per-frame)
    VkDescriptorBufferInfo drawInfo{};
    drawInfo.buffer = frame.indirectBuffer;
    drawInfo.offset = 0;
    drawInfo.range = sizeof(GPUDrawCommand) * MAX_DRAWS;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = frame.cullingDescSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &drawInfo;

    // Binding 4: Draw count (per-frame)
    VkDescriptorBufferInfo countInfo{};
    countInfo.buffer = frame.drawCountBuffer;
    countInfo.offset = 0;
    countInfo.range = sizeof(uint32_t);

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = frame.cullingDescSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &countInfo;

    // Binding 5: Visible clusters (per-frame)
    VkDescriptorBufferInfo visibleInfo{};
    visibleInfo.buffer = frame.visibleClusterBuffer;
    visibleInfo.offset = 0;
    visibleInfo.range = sizeof(uint32_t) * MAX_CLUSTERS;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = frame.cullingDescSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &visibleInfo;

    // Binding 6: Hi-Z pyramid (if available)
    VkDescriptorImageInfo hizImageInfo{};
    hizImageInfo.sampler = m_hizSampler;
    hizImageInfo.imageView = m_hizImageView;
    hizImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = frame.cullingDescSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].descriptorCount = 1;
    writes[6].pImageInfo = &hizImageInfo;

    // Only update Hi-Z binding if resources exist
    uint32_t writeCount = (m_hizImageView != VK_NULL_HANDLE && m_hizSampler != VK_NULL_HANDLE) ? 7 : 6;
    vkUpdateDescriptorSets(m_device, writeCount, writes.data(), 0, nullptr);
}

bool VirtualGeoRenderer::rebuildMergedBuffers() {
    if (m_meshes.empty()) {
        m_mergedData.dirty = false;
        return true;
    }

    std::cout << "[VirtualGeo] Rebuilding merged buffers for GPU-driven rendering..." << std::endl;

    // Clean up existing merged buffers
    cleanupMergedBuffers();

    // Calculate total sizes
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    uint32_t totalClusters = 0;

    for (const auto& [meshId, mesh] : m_meshes) {
        totalVertices += mesh.vertexCount;
        totalIndices += mesh.indexCount;
        totalClusters += mesh.clusterCount;
    }

    if (totalClusters == 0) {
        m_mergedData.dirty = false;
        return true;
    }

    m_mergedData.totalVertices = totalVertices;
    m_mergedData.totalIndices = totalIndices;
    m_mergedData.totalClusters = totalClusters;

    std::cout << "  Total vertices: " << totalVertices << std::endl;
    std::cout << "  Total indices: " << totalIndices << std::endl;
    std::cout << "  Total clusters: " << totalClusters << std::endl;

    // Build merged data
    std::vector<ClusterVertex> mergedVertices;
    std::vector<uint32_t> mergedIndices;
    std::vector<GPUClusterDataExt> mergedClusters;

    mergedVertices.reserve(totalVertices);
    mergedIndices.reserve(totalIndices);
    mergedClusters.reserve(totalClusters);

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;
    uint32_t globalClusterIndex = 0;

    for (auto& [meshId, mesh] : m_meshes) {
        // Store global offsets for this mesh
        mesh.globalVertexOffset = globalVertexOffset;
        mesh.globalIndexOffset = globalIndexOffset;
        mesh.globalClusterOffset = globalClusterIndex;

        // Copy vertices
        for (const auto& v : mesh.sourceVertices) {
            mergedVertices.push_back(v);
        }

        // Copy indices (already global within mesh, need to add mesh vertex offset)
        for (uint32_t idx : mesh.sourceIndices) {
            mergedIndices.push_back(idx + globalVertexOffset);
        }

        // Create extended cluster data with global offsets
        for (const auto& cluster : mesh.sourceClusters) {
            GPUClusterDataExt extCluster;
            extCluster.boundingSphere = glm::vec4(
                cluster.boundingSphereCenter, cluster.boundingSphereRadius);
            extCluster.aabbMin = glm::vec4(cluster.aabbMin, cluster.lodError);
            extCluster.aabbMax = glm::vec4(cluster.aabbMax, cluster.parentError);
            extCluster.vertexOffset = cluster.vertexOffset + globalVertexOffset;
            extCluster.vertexCount = cluster.vertexCount;
            // Find global index offset for this cluster
            // The cluster's indexOffset is relative to mesh's index buffer
            extCluster.globalIndexOffset = cluster.indexOffset + globalIndexOffset;
            extCluster.triangleCount = cluster.triangleCount;
            extCluster.lodLevel = cluster.lodLevel;
            extCluster.materialIndex = cluster.materialIndex;
            extCluster.flags = cluster.flags;
            extCluster.instanceId = 0;  // Will be set per-instance during culling

            mergedClusters.push_back(extCluster);
        }

        globalVertexOffset += mesh.vertexCount;
        globalIndexOffset += mesh.indexCount;
        globalClusterIndex += mesh.clusterCount;
    }

    // Create merged vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(ClusterVertex) * mergedVertices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mergedVertices.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_mergedData.vertexBuffer, m_mergedData.vertexMemory);

        m_renderer->copyBuffer(stagingBuffer, m_mergedData.vertexBuffer, bufferSize);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    // Create merged index buffer
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * mergedIndices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mergedIndices.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_mergedData.indexBuffer, m_mergedData.indexMemory);

        m_renderer->copyBuffer(stagingBuffer, m_mergedData.indexBuffer, bufferSize);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    // Create merged cluster buffer (GPUClusterDataExt)
    {
        VkDeviceSize bufferSize = sizeof(GPUClusterDataExt) * mergedClusters.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mergedClusters.data(), bufferSize);
        vkUnmapMemory(m_device, stagingMemory);

        m_renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_mergedData.clusterBuffer, m_mergedData.clusterMemory);

        m_renderer->copyBuffer(stagingBuffer, m_mergedData.clusterBuffer, bufferSize);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    }

    m_mergedData.dirty = false;

    std::cout << "[VirtualGeo] Merged buffer rebuild complete" << std::endl;
    std::cout << "  Merged vertex buffer: " << (sizeof(ClusterVertex) * totalVertices / 1024) << " KB" << std::endl;
    std::cout << "  Merged index buffer: " << (sizeof(uint32_t) * totalIndices / 1024) << " KB" << std::endl;
    std::cout << "  Merged cluster buffer: " << (sizeof(GPUClusterDataExt) * totalClusters / 1024) << " KB" << std::endl;

    // Update per-frame descriptor sets
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        updatePerFrameDescriptorSet(i);
    }

    return true;
}

void VirtualGeoRenderer::cleanupMergedBuffers() {
    if (m_mergedData.vertexBuffer) {
        vkDestroyBuffer(m_device, m_mergedData.vertexBuffer, nullptr);
        m_mergedData.vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_mergedData.indexBuffer) {
        vkDestroyBuffer(m_device, m_mergedData.indexBuffer, nullptr);
        m_mergedData.indexBuffer = VK_NULL_HANDLE;
    }
    if (m_mergedData.clusterBuffer) {
        vkDestroyBuffer(m_device, m_mergedData.clusterBuffer, nullptr);
        m_mergedData.clusterBuffer = VK_NULL_HANDLE;
    }
    if (m_mergedData.vertexMemory) {
        vkFreeMemory(m_device, m_mergedData.vertexMemory, nullptr);
        m_mergedData.vertexMemory = VK_NULL_HANDLE;
    }
    if (m_mergedData.indexMemory) {
        vkFreeMemory(m_device, m_mergedData.indexMemory, nullptr);
        m_mergedData.indexMemory = VK_NULL_HANDLE;
    }
    if (m_mergedData.clusterMemory) {
        vkFreeMemory(m_device, m_mergedData.clusterMemory, nullptr);
        m_mergedData.clusterMemory = VK_NULL_HANDLE;
    }
    m_mergedData.totalVertices = 0;
    m_mergedData.totalIndices = 0;
    m_mergedData.totalClusters = 0;
}

// ============================================================================
// Hi-Z Occlusion Culling
// ============================================================================

bool VirtualGeoRenderer::createHiZResources(uint32_t width, uint32_t height) {
    // Clean up existing resources
    cleanupHiZResources();

    // Calculate mip levels (power of 2)
    m_hizWidth = width;
    m_hizHeight = height;
    m_hizMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    std::cout << "[VirtualGeo] Creating Hi-Z pyramid: " << width << "x" << height
              << " (" << m_hizMipLevels << " mip levels)" << std::endl;

    // Create Hi-Z image with all mip levels
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = m_hizMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_STORAGE_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // For Hi-Z copy render pass
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_hizImage) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z image" << std::endl;
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_hizImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_hizMemory) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to allocate Hi-Z memory" << std::endl;
        return false;
    }

    vkBindImageMemory(m_device, m_hizImage, m_hizMemory, 0);

    // Initialize Hi-Z image with 1.0 (far depth) and transition to SHADER_READ_ONLY_OPTIMAL
    // This ensures nothing gets incorrectly culled when occlusion culling is enabled
    // but buildHiZPyramid hasn't been called yet
    {
        VkCommandBuffer cmd = m_renderer->beginSingleTimeCommands();

        // First transition to TRANSFER_DST for clearing
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_hizImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_hizMipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        // Clear all mip levels to 1.0 (far depth - nothing occluded)
        VkClearColorValue clearColor = {1.0f, 0.0f, 0.0f, 0.0f};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = m_hizMipLevels;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        vkCmdClearColorImage(cmd, m_hizImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &clearColor, 1, &range);

        // Transition to SHADER_READ_ONLY_OPTIMAL for sampling
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        m_renderer->endSingleTimeCommands(cmd);
    }

    // Create main image view (all mip levels, for sampling)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_hizImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_hizMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_hizImageView) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z image view" << std::endl;
        return false;
    }

    // Create per-mip image views (for compute writes)
    m_hizMipViews.resize(m_hizMipLevels);
    for (uint32_t i = 0; i < m_hizMipLevels; ++i) {
        viewInfo.subresourceRange.baseMipLevel = i;
        viewInfo.subresourceRange.levelCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_hizMipViews[i]) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to create Hi-Z mip view " << i << std::endl;
            return false;
        }
    }

    // Create sampler (for Hi-Z sampling in culling shader)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;  // Point sampling for depth
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_hizMipLevels);

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_hizSampler) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z sampler" << std::endl;
        return false;
    }

    std::cout << "[VirtualGeo] Hi-Z resources created successfully" << std::endl;
    return true;
}

bool VirtualGeoRenderer::createHiZPipeline() {
    // Descriptor set layout for Hi-Z generation
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Binding 0: Input depth/previous mip (sampler2D)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Output mip (storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_hizDescSetLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z descriptor set layout" << std::endl;
        return false;
    }

    // Push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(int32_t) * 6;  // outputSize(2), inputSize(2), mipLevel(1), pad(1)

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_hizDescSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_hizPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z pipeline layout" << std::endl;
        return false;
    }

    // Load shader
    auto shaderCode = m_renderer->readFile("shaders/virtualgeo/hiz_generate.comp.spv");
    if (shaderCode.empty()) {
        std::cerr << "[VirtualGeo] Failed to load Hi-Z compute shader" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = shaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_hizPipelineLayout;

    VkResult result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hizPipeline);
    vkDestroyShaderModule(m_device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z compute pipeline" << std::endl;
        return false;
    }

    std::cout << "[VirtualGeo] Hi-Z pipeline created successfully" << std::endl;
    return true;
}

void VirtualGeoRenderer::cleanupHiZResources() {
    // Clean up debug resources
    if (m_hizDebugPipeline) {
        vkDestroyPipeline(m_device, m_hizDebugPipeline, nullptr);
        m_hizDebugPipeline = VK_NULL_HANDLE;
    }
    if (m_hizDebugPipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_hizDebugPipelineLayout, nullptr);
        m_hizDebugPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_hizDebugDescSetLayout) {
        vkDestroyDescriptorSetLayout(m_device, m_hizDebugDescSetLayout, nullptr);
        m_hizDebugDescSetLayout = VK_NULL_HANDLE;
    }

    // Clean up Hi-Z copy resources
    if (m_hizCopyPipeline) {
        vkDestroyPipeline(m_device, m_hizCopyPipeline, nullptr);
        m_hizCopyPipeline = VK_NULL_HANDLE;
    }
    if (m_hizCopyPipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_hizCopyPipelineLayout, nullptr);
        m_hizCopyPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_hizCopyDescSetLayout) {
        vkDestroyDescriptorSetLayout(m_device, m_hizCopyDescSetLayout, nullptr);
        m_hizCopyDescSetLayout = VK_NULL_HANDLE;
    }
    if (m_hizCopyFramebuffer) {
        vkDestroyFramebuffer(m_device, m_hizCopyFramebuffer, nullptr);
        m_hizCopyFramebuffer = VK_NULL_HANDLE;
    }
    if (m_hizCopyRenderPass) {
        vkDestroyRenderPass(m_device, m_hizCopyRenderPass, nullptr);
        m_hizCopyRenderPass = VK_NULL_HANDLE;
    }

    if (m_hizPipeline) {
        vkDestroyPipeline(m_device, m_hizPipeline, nullptr);
        m_hizPipeline = VK_NULL_HANDLE;
    }
    if (m_hizPipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_hizPipelineLayout, nullptr);
        m_hizPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_hizDescSetLayout) {
        vkDestroyDescriptorSetLayout(m_device, m_hizDescSetLayout, nullptr);
        m_hizDescSetLayout = VK_NULL_HANDLE;
    }
    if (m_hizSampler) {
        vkDestroySampler(m_device, m_hizSampler, nullptr);
        m_hizSampler = VK_NULL_HANDLE;
    }
    for (auto view : m_hizMipViews) {
        if (view) vkDestroyImageView(m_device, view, nullptr);
    }
    m_hizMipViews.clear();
    if (m_hizImageView) {
        vkDestroyImageView(m_device, m_hizImageView, nullptr);
        m_hizImageView = VK_NULL_HANDLE;
    }
    if (m_hizImage) {
        vkDestroyImage(m_device, m_hizImage, nullptr);
        m_hizImage = VK_NULL_HANDLE;
    }
    if (m_hizMemory) {
        vkFreeMemory(m_device, m_hizMemory, nullptr);
        m_hizMemory = VK_NULL_HANDLE;
    }
    m_hizMipLevels = 0;
    m_hizWidth = 0;
    m_hizHeight = 0;
}

void VirtualGeoRenderer::buildHiZPyramid(VkCommandBuffer cmd, VkImageView depthView) {
    if (!m_occlusionCullingEnabled || !m_hizPipeline || m_hizMipLevels == 0) {
        return;
    }

    // Skip Hi-Z on first frame - depth buffer has no valid content yet
    // (temporal occlusion culling needs previous frame's depth)
    if (m_firstFrame) {
        m_firstFrame = false;
        return;
    }

    // Allocate descriptor sets if needed (one per mip level)
    if (m_hizDescSets.size() != m_hizMipLevels) {
        m_hizDescSets.resize(m_hizMipLevels);

        std::vector<VkDescriptorSetLayout> layouts(m_hizMipLevels, m_hizDescSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = m_hizMipLevels;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(m_device, &allocInfo, m_hizDescSets.data()) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to allocate Hi-Z descriptor sets" << std::endl;
            return;
        }
    }

    // =========================================================================
    // Step 1: Use graphics pass to copy depth buffer to Hi-Z mip 0
    // This avoids the issue of sampling depth in compute shaders
    // =========================================================================

    if (m_hizCopyPipeline && m_hizCopyRenderPass && m_hizCopyFramebuffer) {
        // Transition Hi-Z image from previous frame's SHADER_READ_ONLY to GENERAL
        // (or UNDEFINED on first frame - both work with oldLayout = UNDEFINED)
        VkImageMemoryBarrier hizPrepBarrier{};
        hizPrepBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        hizPrepBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Discard previous contents
        hizPrepBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        hizPrepBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hizPrepBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hizPrepBarrier.image = m_hizImage;
        hizPrepBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        hizPrepBarrier.subresourceRange.baseMipLevel = 0;
        hizPrepBarrier.subresourceRange.levelCount = m_hizMipLevels;
        hizPrepBarrier.subresourceRange.baseArrayLayer = 0;
        hizPrepBarrier.subresourceRange.layerCount = 1;
        hizPrepBarrier.srcAccessMask = 0;
        hizPrepBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &hizPrepBarrier);

        // Transition depth buffer for fragment shader sampling
        VkImageMemoryBarrier depthBarrier{};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = m_renderer->getDepthImage();
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &depthBarrier);

        // Update descriptor set for depth texture
        VkDescriptorImageInfo depthImageInfo{};
        depthImageInfo.sampler = m_hizSampler;
        depthImageInfo.imageView = depthView;
        depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_hizCopyDescSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &depthImageInfo;

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

        // Begin render pass to write to Hi-Z mip 0
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_hizCopyRenderPass;
        renderPassInfo.framebuffer = m_hizCopyFramebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {m_hizWidth, m_hizHeight};

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind pipeline and descriptor set
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hizCopyPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_hizCopyPipelineLayout, 0, 1, &m_hizCopyDescSet, 0, nullptr);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);

        // Transition depth buffer back to attachment layout
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &depthBarrier);
    }

    // =========================================================================
    // Step 2: Use compute shader to generate mip levels 1+
    // =========================================================================

    if (m_hizMipLevels <= 1) {
        // Only mip 0, transition to shader read and return
        VkImageMemoryBarrier hizBarrier{};
        hizBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        hizBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        hizBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hizBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hizBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hizBarrier.image = m_hizImage;
        hizBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        hizBarrier.subresourceRange.baseMipLevel = 0;
        hizBarrier.subresourceRange.levelCount = 1;
        hizBarrier.subresourceRange.baseArrayLayer = 0;
        hizBarrier.subresourceRange.layerCount = 1;
        hizBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        hizBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &hizBarrier);
        return;
    }

    // Transition Hi-Z mip 0 from render pass output to compute shader input
    // Also transition remaining mips to GENERAL for compute writes
    VkImageMemoryBarrier hizBarrier{};
    hizBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    hizBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;  // From render pass finalLayout
    hizBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    hizBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hizBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hizBarrier.image = m_hizImage;
    hizBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    hizBarrier.subresourceRange.baseMipLevel = 0;
    hizBarrier.subresourceRange.levelCount = m_hizMipLevels;
    hizBarrier.subresourceRange.baseArrayLayer = 0;
    hizBarrier.subresourceRange.layerCount = 1;
    hizBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    hizBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &hizBarrier);

    // Bind Hi-Z generation pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_hizPipeline);

    // Generate mip levels 1 to N (mip 0 was done by graphics pass)
    uint32_t inputWidth = m_hizWidth;
    uint32_t inputHeight = m_hizHeight;

    for (uint32_t mip = 1; mip < m_hizMipLevels; ++mip) {
        uint32_t outputWidth = std::max(1u, m_hizWidth >> mip);
        uint32_t outputHeight = std::max(1u, m_hizHeight >> mip);

        // Update descriptor set for this mip level
        VkDescriptorImageInfo inputInfo{};
        inputInfo.sampler = m_hizSampler;
        inputInfo.imageView = m_hizMipViews[mip - 1];  // Read from previous mip
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = m_hizMipViews[mip];
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_hizDescSets[mip];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_hizDescSets[mip];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Bind descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_hizPipelineLayout, 0, 1, &m_hizDescSets[mip], 0, nullptr);

        // Push constants
        struct {
            int32_t outputSize[2];
            int32_t inputSize[2];
            uint32_t mipLevel;
            uint32_t pad;
        } pushConstants;

        pushConstants.outputSize[0] = static_cast<int32_t>(outputWidth);
        pushConstants.outputSize[1] = static_cast<int32_t>(outputHeight);
        pushConstants.inputSize[0] = static_cast<int32_t>(inputWidth);
        pushConstants.inputSize[1] = static_cast<int32_t>(inputHeight);
        pushConstants.mipLevel = mip;
        pushConstants.pad = 0;

        vkCmdPushConstants(cmd, m_hizPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(pushConstants), &pushConstants);

        // Dispatch compute
        uint32_t groupsX = (outputWidth + 7) / 8;
        uint32_t groupsY = (outputHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // Memory barrier between mip levels
        if (mip < m_hizMipLevels - 1) {
            VkImageMemoryBarrier mipBarrier{};
            mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.image = m_hizImage;
            mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBarrier.subresourceRange.baseMipLevel = mip;
            mipBarrier.subresourceRange.levelCount = 1;
            mipBarrier.subresourceRange.baseArrayLayer = 0;
            mipBarrier.subresourceRange.layerCount = 1;
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &mipBarrier);
        }

        // Update input size for next iteration
        inputWidth = outputWidth;
        inputHeight = outputHeight;
    }

    // Transition Hi-Z to shader read optimal for culling shader
    hizBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    hizBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hizBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hizBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &hizBarrier);
}

bool VirtualGeoRenderer::createHiZDebugPipeline() {
    // Descriptor set layout for Hi-Z debug visualization
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_hizDebugDescSetLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z debug descriptor set layout" << std::endl;
        return false;
    }

    // Push constants for debug params
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 4;  // mipLevel, depthScale, visualizeMode, padding

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_hizDebugDescSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_hizDebugPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z debug pipeline layout" << std::endl;
        return false;
    }

    // Load shaders
    auto vertCode = m_renderer->readFile("shaders/virtualgeo/hiz_debug.vert.spv");
    auto fragCode = m_renderer->readFile("shaders/virtualgeo/hiz_debug.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "[VirtualGeo] Failed to load Hi-Z debug shaders" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo vertModuleInfo{};
    vertModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertModuleInfo.codeSize = vertCode.size();
    vertModuleInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

    VkShaderModuleCreateInfo fragModuleInfo{};
    fragModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragModuleInfo.codeSize = fragCode.size();
    fragModuleInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());

    VkShaderModule vertModule, fragModule;
    if (vkCreateShaderModule(m_device, &vertModuleInfo, nullptr, &vertModule) != VK_SUCCESS ||
        vkCreateShaderModule(m_device, &fragModuleInfo, nullptr, &fragModule) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z debug shader modules" << std::endl;
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Vertex input (none - fullscreen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_hizDebugPipelineLayout;
    pipelineInfo.renderPass = m_renderer->getRenderPass();
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hizDebugPipeline);

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z debug pipeline" << std::endl;
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_hizDebugDescSetLayout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_hizDebugDescSet) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to allocate Hi-Z debug descriptor set" << std::endl;
        return false;
    }

    std::cout << "[VirtualGeo] Hi-Z debug pipeline created successfully" << std::endl;
    return true;
}

void VirtualGeoRenderer::drawHiZDebug(VkCommandBuffer cmd) {
    if (!m_hizDebugEnabled || !m_hizDebugPipeline || !m_hizImage) {
        return;
    }

    // Update descriptor set to point to Hi-Z pyramid
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_hizSampler;
    imageInfo.imageView = m_hizImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_hizDebugDescSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hizDebugPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hizDebugPipelineLayout,
                            0, 1, &m_hizDebugDescSet, 0, nullptr);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_renderer->getSwapChainExtent().width);
    viewport.height = static_cast<float>(m_renderer->getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_renderer->getSwapChainExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants
    struct {
        float mipLevel;
        float depthScale;
        uint32_t visualizeMode;
        float padding;
    } pushConstants;
    pushConstants.mipLevel = m_hizDebugMipLevel;
    pushConstants.depthScale = 1.0f;  // Linear depth visualization
    pushConstants.visualizeMode = m_hizDebugMode;
    pushConstants.padding = 0.0f;

    vkCmdPushConstants(cmd, m_hizDebugPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pushConstants), &pushConstants);

    // Draw fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool VirtualGeoRenderer::createHiZCopyPipeline() {
    // Create render pass for Hi-Z copy (single R32_SFLOAT attachment)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;  // We transition to GENERAL before render pass
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;    // Stay in GENERAL for compute shader

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_GENERAL;  // Use GENERAL to avoid layout transitions

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_hizCopyRenderPass) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy render pass" << std::endl;
        return false;
    }

    // Create framebuffer targeting Hi-Z mip 0
    if (!m_hizMipViews.empty()) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_hizCopyRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &m_hizMipViews[0];  // Mip 0
        framebufferInfo.width = m_hizWidth;
        framebufferInfo.height = m_hizHeight;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_hizCopyFramebuffer) != VK_SUCCESS) {
            std::cerr << "[VirtualGeo] Failed to create Hi-Z copy framebuffer" << std::endl;
            return false;
        }
    }

    // Descriptor set layout for depth texture sampling
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_hizCopyDescSetLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy descriptor set layout" << std::endl;
        return false;
    }

    // Pipeline layout (no push constants needed)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_hizCopyDescSetLayout;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_hizCopyPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy pipeline layout" << std::endl;
        return false;
    }

    // Load shaders
    auto vertCode = m_renderer->readFile("shaders/virtualgeo/hiz_copy.vert.spv");
    auto fragCode = m_renderer->readFile("shaders/virtualgeo/hiz_copy.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "[VirtualGeo] Failed to load Hi-Z copy shaders" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo vertModuleInfo{};
    vertModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertModuleInfo.codeSize = vertCode.size();
    vertModuleInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

    VkShaderModuleCreateInfo fragModuleInfo{};
    fragModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragModuleInfo.codeSize = fragCode.size();
    fragModuleInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());

    VkShaderModule vertModule, fragModule;
    if (vkCreateShaderModule(m_device, &vertModuleInfo, nullptr, &vertModule) != VK_SUCCESS ||
        vkCreateShaderModule(m_device, &fragModuleInfo, nullptr, &fragModule) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy shader modules" << std::endl;
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Vertex input (none - fullscreen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Use fixed viewport/scissor matching Hi-Z mip 0 size
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_hizWidth);
    viewport.height = static_cast<float>(m_hizHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {m_hizWidth, m_hizHeight};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Only write R channel (depth value)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = m_hizCopyPipelineLayout;
    pipelineInfo.renderPass = m_hizCopyRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hizCopyPipeline);

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to create Hi-Z copy pipeline" << std::endl;
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_hizCopyDescSetLayout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_hizCopyDescSet) != VK_SUCCESS) {
        std::cerr << "[VirtualGeo] Failed to allocate Hi-Z copy descriptor set" << std::endl;
        return false;
    }

    std::cout << "[VirtualGeo] Hi-Z copy pipeline created successfully" << std::endl;
    return true;
}

} // namespace MiEngine
