#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <limits>
#include "material/Material.h"
#include "loader/ModelLoader.h"  // For MeshData

// Axis-Aligned Bounding Box for picking
struct AABB {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 getExtents() const {
        return (max - min) * 0.5f;
    }
};

class Mesh {
public:
    // Construct a Mesh from loaded mesh data
    Mesh(VkDevice device, VkPhysicalDevice physicalDevice,
         const MeshData& meshData,
         const std::shared_ptr<Material>& material = std::make_shared<Material>());
    virtual ~Mesh();

    // Create GPU buffers for vertices and indices using provided command pool and graphics queue
    virtual void createBuffers(VkCommandPool commandPool, VkQueue graphicsQueue);

    // Bind vertex and index buffers to the given command buffer
    virtual void bind(VkCommandBuffer commandBuffer) const;

    // Issue draw command for this mesh
    void draw(VkCommandBuffer commandBuffer) const;

    // Check if this is a skeletal mesh (override in SkeletalMesh)
    virtual bool isSkeletal() const { return false; }

    // Then update the getter and setter:
    // Get mesh material (returns a reference to the shared_ptr)
    const std::shared_ptr<Material>& getMaterial() const { return material; }

    // Set mesh material (takes a shared_ptr to a material)
    void setMaterial(const std::shared_ptr<Material>& newMaterial) { material = newMaterial; }

    // Get the bounding box for picking
    const AABB& getBoundingBox() const { return boundingBox; }

    uint32_t indexCount;
    uint32_t vertexCount = 0;

    // Getters for RT system to copy geometry data
    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }

protected:
    // Protected constructor for derived classes
    Mesh(VkDevice device, VkPhysicalDevice physicalDevice,
         const std::shared_ptr<Material>& material);

    VkDevice device;
    VkPhysicalDevice physicalDevice;
    std::shared_ptr<Material> material;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    // Bounding box for picking (computed before vertices are cleared)
    AABB boundingBox;

    // Internal helpers (protected for derived classes)
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkCommandPool commandPool, VkQueue graphicsQueue,
                    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

private:
    // Local copies of the mesh data
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Compute bounding box from vertices
    void computeBoundingBox();

    // Internal helpers
    void createVertexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue);
    void createIndexBuffer(VkCommandPool commandPool, VkQueue graphicsQueue);
};
