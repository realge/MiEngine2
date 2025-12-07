#pragma once

#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations (global namespace)
class Mesh;        // Global namespace - used as ::Mesh
class VulkanRenderer;

namespace MiEngine {

// Forward declaration for MiEngine namespace class
class SkeletalMesh;

/**
 * MeshLibrary provides runtime caching of loaded meshes.
 * When the same model is loaded multiple times, GPU buffers are shared.
 *
 * Uses weak_ptr to allow meshes to be unloaded when no longer referenced.
 */
class MeshLibrary {
public:
    MeshLibrary(VulkanRenderer* renderer);
    ~MeshLibrary() = default;

    // Get or load a static mesh
    // Returns cached mesh if available, otherwise loads from cache/FBX
    std::shared_ptr<::Mesh> getMesh(const std::string& assetPath);

    // Get or load a skeletal mesh
    std::shared_ptr<SkeletalMesh> getSkeletalMesh(const std::string& assetPath);

    // Check if a mesh is already loaded
    bool isMeshLoaded(const std::string& assetPath) const;
    bool isSkeletalMeshLoaded(const std::string& assetPath) const;

    // Force reload a mesh (bypasses cache)
    std::shared_ptr<::Mesh> reloadMesh(const std::string& assetPath);
    std::shared_ptr<SkeletalMesh> reloadSkeletalMesh(const std::string& assetPath);

    // Remove expired weak pointers (cleanup)
    void collectGarbage();

    // Clear all cached meshes
    void clear();

    // Statistics
    size_t getLoadedMeshCount() const;
    size_t getLoadedSkeletalMeshCount() const;
    size_t getTotalLoadedCount() const;

private:
    // Load mesh from cache or FBX file
    std::shared_ptr<::Mesh> loadMeshInternal(const std::string& assetPath);
    std::shared_ptr<SkeletalMesh> loadSkeletalMeshInternal(const std::string& assetPath);

    // Create primitive mesh (sphere, cube, plane, etc.)
    std::shared_ptr<::Mesh> createPrimitiveMesh(const std::string& primitiveType);

    VulkanRenderer* m_renderer;

    // Weak pointers allow meshes to be unloaded when not referenced elsewhere
    std::unordered_map<std::string, std::weak_ptr<::Mesh>> m_meshCache;
    std::unordered_map<std::string, std::weak_ptr<SkeletalMesh>> m_skeletalMeshCache;
};

} // namespace MiEngine
