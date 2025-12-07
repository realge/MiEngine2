#include "mesh/SkeletalMesh.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace MiEngine {

SkeletalMesh::SkeletalMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                           const SkeletalMeshData& meshData,
                           const std::shared_ptr<Material>& material)
    : Mesh(device, physicalDevice, material)
    , m_skeletalVertices(meshData.vertices)
    , m_skeletalIndices(meshData.indices) {

    indexCount = static_cast<uint32_t>(m_skeletalIndices.size());
    computeBoundingBox();
}

SkeletalMesh::~SkeletalMesh() {
    // Base class destructor handles buffer cleanup
}

void SkeletalMesh::computeBoundingBox() {
    for (const auto& vertex : m_skeletalVertices) {
        boundingBox.expand(vertex.position);
    }
}

void SkeletalMesh::createBuffers(VkCommandPool commandPool, VkQueue graphicsQueue) {
    createVertexBuffer(commandPool, graphicsQueue);
    createIndexBuffer(commandPool, graphicsQueue);

    // Clear local copies after upload to GPU
    m_skeletalVertices.clear();
    m_skeletalVertices.shrink_to_fit();
    m_skeletalIndices.clear();
    m_skeletalIndices.shrink_to_fit();
}

void SkeletalMesh::createVertexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue) {
    VkDeviceSize bufferSize = sizeof(SkeletalVertex) * m_skeletalVertices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    // Map and copy data
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_skeletalVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Create device-local vertex buffer
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 vertexBuffer, vertexBufferMemory);

    // Copy from staging to device-local
    copyBuffer(commandPool, graphicsQueue, stagingBuffer, vertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void SkeletalMesh::createIndexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue) {
    VkDeviceSize bufferSize = sizeof(uint32_t) * m_skeletalIndices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    // Map and copy data
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_skeletalIndices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Create device-local index buffer
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 indexBuffer, indexBufferMemory);

    // Copy from staging to device-local
    copyBuffer(commandPool, graphicsQueue, stagingBuffer, indexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void SkeletalMesh::bind(VkCommandBuffer commandBuffer) const {
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

} // namespace MiEngine
