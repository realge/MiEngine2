#include "asset/MeshLibrary.h"
#include "asset/MeshCache.h"
#include "asset/AssetRegistry.h"
#include "mesh/Mesh.h"
#include "mesh/SkeletalMesh.h"
#include "loader/ModelLoader.h"
#include "VulkanRenderer.h"
#include "project/ProjectManager.h"
#include <iostream>
#include <algorithm>
#include <cctype>

namespace MiEngine {

MeshLibrary::MeshLibrary(VulkanRenderer* renderer)
    : m_renderer(renderer) {
}

std::shared_ptr<::Mesh> MeshLibrary::getMesh(const std::string& assetPath) {
    // Check if already cached and still alive
    auto it = m_meshCache.find(assetPath);
    if (it != m_meshCache.end()) {
        auto mesh = it->second.lock();
        if (mesh) {
            return mesh;
        }
        // Expired, will reload below
    }

    // Load and cache
    auto mesh = loadMeshInternal(assetPath);
    if (mesh) {
        m_meshCache[assetPath] = mesh;
    }
    return mesh;
}

std::shared_ptr<SkeletalMesh> MeshLibrary::getSkeletalMesh(const std::string& assetPath) {
    // Check if already cached and still alive
    auto it = m_skeletalMeshCache.find(assetPath);
    if (it != m_skeletalMeshCache.end()) {
        auto mesh = it->second.lock();
        if (mesh) {
            return mesh;
        }
    }

    // Load and cache
    auto mesh = loadSkeletalMeshInternal(assetPath);
    if (mesh) {
        m_skeletalMeshCache[assetPath] = mesh;
    }
    return mesh;
}

bool MeshLibrary::isMeshLoaded(const std::string& assetPath) const {
    auto it = m_meshCache.find(assetPath);
    if (it != m_meshCache.end()) {
        return !it->second.expired();
    }
    return false;
}

bool MeshLibrary::isSkeletalMeshLoaded(const std::string& assetPath) const {
    auto it = m_skeletalMeshCache.find(assetPath);
    if (it != m_skeletalMeshCache.end()) {
        return !it->second.expired();
    }
    return false;
}

std::shared_ptr<::Mesh> MeshLibrary::reloadMesh(const std::string& assetPath) {
    m_meshCache.erase(assetPath);
    return getMesh(assetPath);
}

std::shared_ptr<SkeletalMesh> MeshLibrary::reloadSkeletalMesh(const std::string& assetPath) {
    m_skeletalMeshCache.erase(assetPath);
    return getSkeletalMesh(assetPath);
}

void MeshLibrary::collectGarbage() {
    // Remove expired entries from mesh cache
    for (auto it = m_meshCache.begin(); it != m_meshCache.end(); ) {
        if (it->second.expired()) {
            it = m_meshCache.erase(it);
        } else {
            ++it;
        }
    }

    // Remove expired entries from skeletal mesh cache
    for (auto it = m_skeletalMeshCache.begin(); it != m_skeletalMeshCache.end(); ) {
        if (it->second.expired()) {
            it = m_skeletalMeshCache.erase(it);
        } else {
            ++it;
        }
    }
}

void MeshLibrary::clear() {
    m_meshCache.clear();
    m_skeletalMeshCache.clear();
}

size_t MeshLibrary::getLoadedMeshCount() const {
    size_t count = 0;
    for (const auto& pair : m_meshCache) {
        if (!pair.second.expired()) {
            ++count;
        }
    }
    return count;
}

size_t MeshLibrary::getLoadedSkeletalMeshCount() const {
    size_t count = 0;
    for (const auto& pair : m_skeletalMeshCache) {
        if (!pair.second.expired()) {
            ++count;
        }
    }
    return count;
}

size_t MeshLibrary::getTotalLoadedCount() const {
    return getLoadedMeshCount() + getLoadedSkeletalMeshCount();
}

std::shared_ptr<::Mesh> MeshLibrary::loadMeshInternal(const std::string& assetPath) {
    if (!m_renderer) {
        std::cerr << "MeshLibrary: No renderer set" << std::endl;
        return nullptr;
    }

    // Check for primitive mesh types first
    std::shared_ptr<::Mesh> primitiveMesh = createPrimitiveMesh(assetPath);
    if (primitiveMesh) {
        return primitiveMesh;
    }

    // Resolve path - first check if it's an absolute path
    fs::path sourcePath;
    if (fs::path(assetPath).is_absolute() && fs::exists(assetPath)) {
        sourcePath = assetPath;
    } else {
        auto& pm = ProjectManager::getInstance();

        // Try multiple resolution strategies:
        // 1. Direct path under Assets folder (e.g., "Models/test.fbx" -> "Assets/Models/test.fbx")
        if (pm.hasProject()) {
            fs::path directPath = pm.getCurrentProject()->getAssetsPath() / assetPath;
            if (fs::exists(directPath)) {
                sourcePath = directPath;
            }
        }

        // 2. Try resolveModelPath (looks in Assets/Models)
        if (sourcePath.empty()) {
            auto resolved = pm.resolveModelPath(assetPath);
            if (resolved) {
                sourcePath = *resolved;
            }
        }

        // 3. Strip "Models/" prefix if present and try again
        if (sourcePath.empty() && assetPath.find("Models/") == 0) {
            std::string stripped = assetPath.substr(7); // Remove "Models/"
            auto resolved = pm.resolveModelPath(stripped);
            if (resolved) {
                sourcePath = *resolved;
            }
        }

        // 4. Try as relative path from current directory
        if (sourcePath.empty() && fs::exists(assetPath)) {
            sourcePath = assetPath;
        }

        if (sourcePath.empty()) {
            std::cerr << "MeshLibrary: Model not found: " << assetPath << std::endl;
            return nullptr;
        }
    }

    // Check for cached binary first
    auto& registry = AssetRegistry::getInstance();
    const AssetEntry* entry = registry.findByPath(assetPath);

    std::vector<MeshData> meshDataList;

    if (entry && entry->cacheValid) {
        fs::path cachePath = registry.resolveCachePath(entry->cachePath);
        if (MeshCache::isValid(cachePath, sourcePath)) {
            if (MeshCache::load(cachePath, meshDataList)) {
                std::cout << "MeshLibrary: Loaded from cache: " << assetPath << std::endl;
            }
        }
    }

    // If cache miss, load from FBX
    if (meshDataList.empty()) {
        ModelLoader loader;
        if (!loader.LoadModel(sourcePath.string())) {
            std::cerr << "MeshLibrary: Failed to load model: " << sourcePath << std::endl;
            return nullptr;
        }
        meshDataList = loader.GetMeshData();
        std::cout << "MeshLibrary: Loaded from FBX: " << assetPath << std::endl;
    }

    if (meshDataList.empty()) {
        std::cerr << "MeshLibrary: No mesh data in: " << assetPath << std::endl;
        return nullptr;
    }

    // Create GPU mesh from first submesh (TODO: support multiple submeshes)
    VkDevice device = m_renderer->getDevice();
    VkPhysicalDevice physicalDevice = m_renderer->getPhysicalDevice();
    VkCommandPool commandPool = m_renderer->getCommandPool();
    VkQueue graphicsQueue = m_renderer->getGraphicsQueue();

    auto mesh = std::make_shared<::Mesh>(device, physicalDevice, meshDataList[0]);
    mesh->createBuffers(commandPool, graphicsQueue);

    return mesh;
}

std::shared_ptr<SkeletalMesh> MeshLibrary::loadSkeletalMeshInternal(const std::string& assetPath) {
    if (!m_renderer) {
        std::cerr << "MeshLibrary: No renderer set" << std::endl;
        return nullptr;
    }

    // Resolve path - first check if it's an absolute path
    fs::path sourcePath;
    if (fs::path(assetPath).is_absolute() && fs::exists(assetPath)) {
        sourcePath = assetPath;
    } else {
        auto& pm = ProjectManager::getInstance();

        // Try multiple resolution strategies:
        // 1. Direct path under Assets folder (e.g., "Models/test.fbx" -> "Assets/Models/test.fbx")
        if (pm.hasProject()) {
            fs::path directPath = pm.getCurrentProject()->getAssetsPath() / assetPath;
            if (fs::exists(directPath)) {
                sourcePath = directPath;
            }
        }

        // 2. Try resolveModelPath (looks in Assets/Models)
        if (sourcePath.empty()) {
            auto resolved = pm.resolveModelPath(assetPath);
            if (resolved) {
                sourcePath = *resolved;
            }
        }

        // 3. Strip "Models/" prefix if present and try again
        if (sourcePath.empty() && assetPath.find("Models/") == 0) {
            std::string stripped = assetPath.substr(7); // Remove "Models/"
            auto resolved = pm.resolveModelPath(stripped);
            if (resolved) {
                sourcePath = *resolved;
            }
        }

        // 4. Try as relative path from current directory
        if (sourcePath.empty() && fs::exists(assetPath)) {
            sourcePath = assetPath;
        }

        if (sourcePath.empty()) {
            std::cerr << "MeshLibrary: Skeletal model not found: " << assetPath << std::endl;
            return nullptr;
        }
    }

    // Check for cached binary first
    auto& registry = AssetRegistry::getInstance();
    const AssetEntry* entry = registry.findByPath(assetPath);

    SkeletalModelData modelData;
    bool loadedFromCache = false;

    if (entry && entry->cacheValid) {
        fs::path cachePath = registry.resolveCachePath(entry->cachePath);
        if (MeshCache::isValid(cachePath, sourcePath)) {
            if (MeshCache::loadSkeletal(cachePath, modelData)) {
                std::cout << "MeshLibrary: Loaded skeletal from cache: " << assetPath << std::endl;
                loadedFromCache = true;
            }
        }
    }

    // If cache miss, load from FBX
    if (!loadedFromCache) {
        ModelLoader loader;
        if (!loader.LoadSkeletalModel(sourcePath.string(), modelData)) {
            std::cerr << "MeshLibrary: Failed to load skeletal model: " << sourcePath << std::endl;
            return nullptr;
        }
        std::cout << "MeshLibrary: Loaded skeletal from FBX: " << assetPath << std::endl;
    }

    if (modelData.meshes.empty()) {
        std::cerr << "MeshLibrary: No skeletal mesh data in: " << assetPath << std::endl;
        return nullptr;
    }

    // Create GPU skeletal mesh from first submesh
    VkDevice device = m_renderer->getDevice();
    VkPhysicalDevice physicalDevice = m_renderer->getPhysicalDevice();
    VkCommandPool commandPool = m_renderer->getCommandPool();
    VkQueue graphicsQueue = m_renderer->getGraphicsQueue();

    auto mesh = std::make_shared<SkeletalMesh>(device, physicalDevice, modelData.meshes[0]);
    mesh->createBuffers(commandPool, graphicsQueue);

    return mesh;
}

std::shared_ptr<::Mesh> MeshLibrary::createPrimitiveMesh(const std::string& primitiveType) {
    ModelLoader loader;
    MeshData meshData;
    bool isPrimitive = false;

    // Convert to lowercase for comparison
    std::string lowerType = primitiveType;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);

    if (lowerType == "sphere") {
        meshData = loader.CreateSphere(1.0f, 32, 32);
        isPrimitive = true;
    } else if (lowerType == "cube" || lowerType == "box") {
        meshData = loader.CreateCube(1.0f);
        isPrimitive = true;
    } else if (lowerType == "plane" || lowerType == "quad") {
        meshData = loader.CreatePlane(1.0f, 1.0f);
        isPrimitive = true;
    }

    if (!isPrimitive) {
        return nullptr;
    }

    // Create GPU mesh
    VkDevice device = m_renderer->getDevice();
    VkPhysicalDevice physicalDevice = m_renderer->getPhysicalDevice();
    VkCommandPool commandPool = m_renderer->getCommandPool();
    VkQueue graphicsQueue = m_renderer->getGraphicsQueue();

    auto mesh = std::make_shared<::Mesh>(device, physicalDevice, meshData);
    mesh->createBuffers(commandPool, graphicsQueue);

    std::cout << "MeshLibrary: Created primitive mesh: " << primitiveType << std::endl;
    return mesh;
}

} // namespace MiEngine
