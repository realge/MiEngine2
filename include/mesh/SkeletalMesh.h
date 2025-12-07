#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "mesh/Mesh.h"
#include "Utils/SkeletalVertex.h"
#include "loader/ModelLoader.h"

namespace MiEngine {

/**
 * SkeletalMesh extends Mesh to support skeletal vertex data.
 * Uses SkeletalVertex format with bone indices and weights.
 */
class SkeletalMesh : public Mesh {
public:
    SkeletalMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                 const SkeletalMeshData& meshData,
                 const std::shared_ptr<Material>& material = std::make_shared<Material>());

    virtual ~SkeletalMesh();

    // Override buffer creation to use SkeletalVertex format
    void createBuffers(VkCommandPool commandPool, VkQueue graphicsQueue) override;

    // Override bind to use correct vertex layout
    void bind(VkCommandBuffer commandBuffer) const override;

    // Check if this is a skeletal mesh
    bool isSkeletal() const override { return true; }

    // Get skeletal vertex count
    uint32_t getSkeletalVertexCount() const { return static_cast<uint32_t>(m_skeletalVertices.size()); }

private:
    std::vector<SkeletalVertex> m_skeletalVertices;
    std::vector<uint32_t> m_skeletalIndices;

    void computeBoundingBox();
    void createVertexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue);
    void createIndexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue);
};

} // namespace MiEngine
