#include "include/virtualgeo/ClusteredMeshCache.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>

namespace MiEngine {

// ============================================================================
// Save Operations
// ============================================================================

bool ClusteredMeshCache::save(const fs::path& cachePath,
                               const ClusteredMesh& mesh,
                               const fs::path& sourcePath) {
    // Create parent directories if needed
    if (cachePath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(cachePath.parent_path(), ec);
        if (ec) {
            std::cerr << "ClusteredMeshCache: Failed to create cache directory: "
                      << ec.message() << std::endl;
            return false;
        }
    }

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ClusteredMeshCache: Failed to open file for writing: "
                  << cachePath << std::endl;
        return false;
    }

    // Build header
    ClusteredMeshCacheHeader header{};
    std::memcpy(header.magic, MAGIC, 8);
    header.version = VERSION;
    header.flags = 0;
    header.sourceFileHash = computeSourceHash(sourcePath);
    header.sourceModTime = getSourceModTime(sourcePath);

    header.clusterCount = static_cast<uint32_t>(mesh.clusters.size());
    header.groupCount = static_cast<uint32_t>(mesh.groups.size());
    header.maxLodLevel = mesh.maxLodLevel;

    header.totalVertices = static_cast<uint32_t>(mesh.vertices.size());
    header.totalIndices = static_cast<uint32_t>(mesh.indices.size());
    header.totalTriangles = mesh.totalTriangles;

    header.rootClusterStart = mesh.rootClusterStart;
    header.rootClusterCount = mesh.rootClusterCount;
    header.leafClusterStart = mesh.leafClusterStart;
    header.leafClusterCount = mesh.leafClusterCount;

    header.boundingSphereCenter[0] = mesh.boundingSphereCenter.x;
    header.boundingSphereCenter[1] = mesh.boundingSphereCenter.y;
    header.boundingSphereCenter[2] = mesh.boundingSphereCenter.z;
    header.boundingSphereRadius = mesh.boundingSphereRadius;

    header.aabbMin[0] = mesh.aabbMin.x;
    header.aabbMin[1] = mesh.aabbMin.y;
    header.aabbMin[2] = mesh.aabbMin.z;
    header.aabbMax[0] = mesh.aabbMax.x;
    header.aabbMax[1] = mesh.aabbMax.y;
    header.aabbMax[2] = mesh.aabbMax.z;

    header.maxError = mesh.maxError;
    header.minError = mesh.minError;

    // Write header
    if (!writeHeader(file, header)) {
        std::cerr << "ClusteredMeshCache: Failed to write header" << std::endl;
        return false;
    }

    // Write mesh name
    if (!writeString(file, mesh.name)) {
        std::cerr << "ClusteredMeshCache: Failed to write mesh name" << std::endl;
        return false;
    }

    // Write all clusters
    for (const auto& cluster : mesh.clusters) {
        if (!writeCluster(file, cluster)) {
            std::cerr << "ClusteredMeshCache: Failed to write cluster" << std::endl;
            return false;
        }
    }

    // Write all groups (if any)
    for (const auto& group : mesh.groups) {
        if (!writeGroup(file, group)) {
            std::cerr << "ClusteredMeshCache: Failed to write group" << std::endl;
            return false;
        }
    }

    // Write vertices
    if (!writeVertices(file, mesh.vertices)) {
        std::cerr << "ClusteredMeshCache: Failed to write vertices" << std::endl;
        return false;
    }

    // Write indices
    if (!writeIndices(file, mesh.indices)) {
        std::cerr << "ClusteredMeshCache: Failed to write indices" << std::endl;
        return false;
    }

    file.close();

    std::cout << "ClusteredMeshCache: Saved " << mesh.name << " to " << cachePath << std::endl;
    std::cout << "  Clusters: " << mesh.clusters.size() << std::endl;
    std::cout << "  Vertices: " << mesh.vertices.size() << std::endl;
    std::cout << "  Indices: " << mesh.indices.size() << std::endl;
    std::cout << "  LOD levels: " << mesh.maxLodLevel + 1 << std::endl;

    return true;
}

// ============================================================================
// Load Operations
// ============================================================================

bool ClusteredMeshCache::load(const fs::path& cachePath,
                               ClusteredMesh& outMesh) {
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ClusteredMeshCache: Failed to open file for reading: "
                  << cachePath << std::endl;
        return false;
    }

    // Read header
    ClusteredMeshCacheHeader header{};
    if (!readHeader(file, header)) {
        std::cerr << "ClusteredMeshCache: Failed to read header" << std::endl;
        return false;
    }

    // Verify magic
    if (std::memcmp(header.magic, MAGIC, 8) != 0) {
        std::cerr << "ClusteredMeshCache: Invalid magic number" << std::endl;
        return false;
    }

    // Check version
    if (header.version != VERSION) {
        std::cerr << "ClusteredMeshCache: Version mismatch (file: " << header.version
                  << ", expected: " << VERSION << ")" << std::endl;
        return false;
    }

    // Read mesh name
    if (!readString(file, outMesh.name)) {
        std::cerr << "ClusteredMeshCache: Failed to read mesh name" << std::endl;
        return false;
    }

    // Populate mesh metadata from header
    outMesh.meshId = 0;  // Will be assigned by caller
    outMesh.maxLodLevel = header.maxLodLevel;
    outMesh.rootClusterStart = header.rootClusterStart;
    outMesh.rootClusterCount = header.rootClusterCount;
    outMesh.leafClusterStart = header.leafClusterStart;
    outMesh.leafClusterCount = header.leafClusterCount;
    outMesh.totalTriangles = header.totalTriangles;
    outMesh.totalVertices = header.totalVertices;

    outMesh.boundingSphereCenter = glm::vec3(
        header.boundingSphereCenter[0],
        header.boundingSphereCenter[1],
        header.boundingSphereCenter[2]
    );
    outMesh.boundingSphereRadius = header.boundingSphereRadius;

    outMesh.aabbMin = glm::vec3(header.aabbMin[0], header.aabbMin[1], header.aabbMin[2]);
    outMesh.aabbMax = glm::vec3(header.aabbMax[0], header.aabbMax[1], header.aabbMax[2]);

    outMesh.maxError = header.maxError;
    outMesh.minError = header.minError;

    // Read clusters
    outMesh.clusters.resize(header.clusterCount);
    for (uint32_t i = 0; i < header.clusterCount; i++) {
        if (!readCluster(file, outMesh.clusters[i])) {
            std::cerr << "ClusteredMeshCache: Failed to read cluster " << i << std::endl;
            return false;
        }
    }

    // Read groups
    outMesh.groups.resize(header.groupCount);
    for (uint32_t i = 0; i < header.groupCount; i++) {
        if (!readGroup(file, outMesh.groups[i])) {
            std::cerr << "ClusteredMeshCache: Failed to read group " << i << std::endl;
            return false;
        }
    }

    // Read vertices
    if (!readVertices(file, outMesh.vertices, header.totalVertices)) {
        std::cerr << "ClusteredMeshCache: Failed to read vertices" << std::endl;
        return false;
    }

    // Read indices
    if (!readIndices(file, outMesh.indices, header.totalIndices)) {
        std::cerr << "ClusteredMeshCache: Failed to read indices" << std::endl;
        return false;
    }

    file.close();

    std::cout << "ClusteredMeshCache: Loaded " << outMesh.name << " from " << cachePath << std::endl;
    std::cout << "  Clusters: " << outMesh.clusters.size() << std::endl;
    std::cout << "  Vertices: " << outMesh.vertices.size() << std::endl;
    std::cout << "  LOD levels: " << outMesh.maxLodLevel + 1 << std::endl;

    return true;
}

// ============================================================================
// Cache Validation
// ============================================================================

bool ClusteredMeshCache::isValid(const fs::path& cachePath,
                                  const fs::path& sourcePath) {
    if (!fs::exists(cachePath)) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    ClusteredMeshCacheHeader header{};
    if (!readHeader(file, header)) {
        return false;
    }

    file.close();

    // Verify magic
    if (std::memcmp(header.magic, MAGIC, 8) != 0) {
        return false;
    }

    // Check version
    if (header.version != VERSION) {
        return false;
    }

    // Check if source file has been modified
    uint64_t currentModTime = getSourceModTime(sourcePath);
    if (currentModTime != header.sourceModTime) {
        return false;
    }

    // Verify source hash matches
    uint64_t currentHash = computeSourceHash(sourcePath);
    if (currentHash != header.sourceFileHash) {
        return false;
    }

    return true;
}

bool ClusteredMeshCache::exists(const fs::path& cachePath) {
    if (!fs::exists(cachePath)) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    ClusteredMeshCacheHeader header{};
    if (!readHeader(file, header)) {
        return false;
    }

    file.close();

    // Verify magic and version
    return std::memcmp(header.magic, MAGIC, 8) == 0 && header.version == VERSION;
}

// ============================================================================
// Path Utilities
// ============================================================================

fs::path ClusteredMeshCache::getCachePath(const fs::path& sourcePath,
                                           const fs::path& cacheDir) {
    // Generate filename: basename + hash suffix + extension
    std::string baseName = sourcePath.stem().string();
    uint64_t hash = computeSourceHash(sourcePath);

    // Convert hash to short hex string (6 chars)
    char hashStr[8];
    snprintf(hashStr, sizeof(hashStr), "%06llx",
             static_cast<unsigned long long>(hash & 0xFFFFFF));

    std::string cacheName = baseName + "_" + hashStr + EXTENSION;
    return cacheDir / cacheName;
}

uint64_t ClusteredMeshCache::computeSourceHash(const fs::path& sourcePath) {
    // Simple FNV-1a hash of the path string
    std::string pathStr = sourcePath.string();

    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : pathStr) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}

uint64_t ClusteredMeshCache::getSourceModTime(const fs::path& sourcePath) {
    if (!fs::exists(sourcePath)) {
        return 0;
    }

    auto ftime = fs::last_write_time(sourcePath);
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::clock_cast<std::chrono::system_clock>(ftime)
    );
    return static_cast<uint64_t>(sctp.time_since_epoch().count());
}

// ============================================================================
// Debug/Info
// ============================================================================

void ClusteredMeshCache::printInfo(const fs::path& cachePath) {
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "ClusteredMeshCache: Cannot open " << cachePath << std::endl;
        return;
    }

    ClusteredMeshCacheHeader header{};
    if (!readHeader(file, header)) {
        std::cout << "ClusteredMeshCache: Invalid header in " << cachePath << std::endl;
        return;
    }

    std::string meshName;
    readString(file, meshName);

    file.close();

    std::cout << "=== Clustered Mesh Cache Info ===" << std::endl;
    std::cout << "File: " << cachePath << std::endl;
    std::cout << "Magic: " << std::string(header.magic, 8) << std::endl;
    std::cout << "Version: " << header.version << std::endl;
    std::cout << "Mesh Name: " << meshName << std::endl;
    std::cout << "Clusters: " << header.clusterCount << std::endl;
    std::cout << "Groups: " << header.groupCount << std::endl;
    std::cout << "LOD Levels: " << header.maxLodLevel + 1 << std::endl;
    std::cout << "Vertices: " << header.totalVertices << std::endl;
    std::cout << "Indices: " << header.totalIndices << std::endl;
    std::cout << "Triangles: " << header.totalTriangles << std::endl;
    std::cout << "Root Clusters: " << header.rootClusterCount << std::endl;
    std::cout << "Leaf Clusters: " << header.leafClusterCount << std::endl;
    std::cout << "Bounding Sphere: center=("
              << header.boundingSphereCenter[0] << ", "
              << header.boundingSphereCenter[1] << ", "
              << header.boundingSphereCenter[2] << "), radius="
              << header.boundingSphereRadius << std::endl;
    std::cout << "Error Range: " << header.minError << " - " << header.maxError << std::endl;
    std::cout << "=================================" << std::endl;
}

// ============================================================================
// Write Helpers
// ============================================================================

bool ClusteredMeshCache::writeHeader(std::ofstream& file,
                                      const ClusteredMeshCacheHeader& header) {
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return file.good();
}

bool ClusteredMeshCache::writeString(std::ofstream& file, const std::string& str) {
    uint32_t length = static_cast<uint32_t>(str.length());
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    if (length > 0) {
        file.write(str.data(), length);
    }
    return file.good();
}

bool ClusteredMeshCache::writeCluster(std::ofstream& file, const Cluster& cluster) {
    ClusterChunkHeader chunk{};

    chunk.clusterId = cluster.clusterId;
    chunk.lodLevel = cluster.lodLevel;
    chunk.meshId = cluster.meshId;

    chunk.vertexOffset = cluster.vertexOffset;
    chunk.vertexCount = cluster.vertexCount;
    chunk.indexOffset = cluster.indexOffset;
    chunk.triangleCount = cluster.triangleCount;

    chunk.boundingSphereCenter[0] = cluster.boundingSphereCenter.x;
    chunk.boundingSphereCenter[1] = cluster.boundingSphereCenter.y;
    chunk.boundingSphereCenter[2] = cluster.boundingSphereCenter.z;
    chunk.boundingSphereRadius = cluster.boundingSphereRadius;

    chunk.aabbMin[0] = cluster.aabbMin.x;
    chunk.aabbMin[1] = cluster.aabbMin.y;
    chunk.aabbMin[2] = cluster.aabbMin.z;
    chunk.aabbMax[0] = cluster.aabbMax.x;
    chunk.aabbMax[1] = cluster.aabbMax.y;
    chunk.aabbMax[2] = cluster.aabbMax.z;

    chunk.lodError = cluster.lodError;
    chunk.parentError = cluster.parentError;
    chunk.screenSpaceError = cluster.screenSpaceError;
    chunk.maxChildError = cluster.maxChildError;

    chunk.parentClusterStart = cluster.parentClusterStart;
    chunk.parentClusterCount = cluster.parentClusterCount;
    chunk.childClusterStart = cluster.childClusterStart;
    chunk.childClusterCount = cluster.childClusterCount;

    chunk.materialIndex = cluster.materialIndex;
    chunk.flags = cluster.flags;

    chunk.debugColor[0] = cluster.debugColor.r;
    chunk.debugColor[1] = cluster.debugColor.g;
    chunk.debugColor[2] = cluster.debugColor.b;
    chunk.debugColor[3] = cluster.debugColor.a;

    file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
    return file.good();
}

bool ClusteredMeshCache::writeGroup(std::ofstream& file, const ClusterGroup& group) {
    ClusterGroupChunkHeader chunk{};

    chunk.groupId = group.groupId;
    chunk.lodLevel = group.lodLevel;
    chunk.clusterStart = group.clusterStart;
    chunk.clusterCount = group.clusterCount;

    chunk.boundingSphereCenter[0] = group.boundingSphereCenter.x;
    chunk.boundingSphereCenter[1] = group.boundingSphereCenter.y;
    chunk.boundingSphereCenter[2] = group.boundingSphereCenter.z;
    chunk.boundingSphereRadius = group.boundingSphereRadius;

    chunk.lodError = group.lodError;
    chunk.parentError = group.parentError;

    chunk.parentGroupStart = group.parentGroupStart;
    chunk.parentGroupCount = group.parentGroupCount;
    chunk.childGroupStart = group.childGroupStart;
    chunk.childGroupCount = group.childGroupCount;

    file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
    return file.good();
}

bool ClusteredMeshCache::writeVertices(std::ofstream& file,
                                        const std::vector<ClusterVertex>& vertices) {
    if (vertices.empty()) return true;

    file.write(reinterpret_cast<const char*>(vertices.data()),
               vertices.size() * sizeof(ClusterVertex));
    return file.good();
}

bool ClusteredMeshCache::writeIndices(std::ofstream& file,
                                       const std::vector<uint32_t>& indices) {
    if (indices.empty()) return true;

    file.write(reinterpret_cast<const char*>(indices.data()),
               indices.size() * sizeof(uint32_t));
    return file.good();
}

// ============================================================================
// Read Helpers
// ============================================================================

bool ClusteredMeshCache::readHeader(std::ifstream& file,
                                     ClusteredMeshCacheHeader& header) {
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    return file.good();
}

bool ClusteredMeshCache::readString(std::ifstream& file, std::string& str) {
    uint32_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    if (!file.good()) return false;

    if (length > 0) {
        str.resize(length);
        file.read(&str[0], length);
    } else {
        str.clear();
    }
    return file.good();
}

bool ClusteredMeshCache::readCluster(std::ifstream& file, Cluster& cluster) {
    ClusterChunkHeader chunk{};
    file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
    if (!file.good()) return false;

    cluster.clusterId = chunk.clusterId;
    cluster.lodLevel = chunk.lodLevel;
    cluster.meshId = chunk.meshId;

    cluster.vertexOffset = chunk.vertexOffset;
    cluster.vertexCount = chunk.vertexCount;
    cluster.indexOffset = chunk.indexOffset;
    cluster.triangleCount = chunk.triangleCount;

    cluster.boundingSphereCenter = glm::vec3(
        chunk.boundingSphereCenter[0],
        chunk.boundingSphereCenter[1],
        chunk.boundingSphereCenter[2]
    );
    cluster.boundingSphereRadius = chunk.boundingSphereRadius;

    cluster.aabbMin = glm::vec3(chunk.aabbMin[0], chunk.aabbMin[1], chunk.aabbMin[2]);
    cluster.aabbMax = glm::vec3(chunk.aabbMax[0], chunk.aabbMax[1], chunk.aabbMax[2]);

    cluster.lodError = chunk.lodError;
    cluster.parentError = chunk.parentError;
    cluster.screenSpaceError = chunk.screenSpaceError;
    cluster.maxChildError = chunk.maxChildError;

    cluster.parentClusterStart = chunk.parentClusterStart;
    cluster.parentClusterCount = chunk.parentClusterCount;
    cluster.childClusterStart = chunk.childClusterStart;
    cluster.childClusterCount = chunk.childClusterCount;

    cluster.materialIndex = chunk.materialIndex;
    cluster.flags = chunk.flags;

    cluster.debugColor = glm::vec4(
        chunk.debugColor[0],
        chunk.debugColor[1],
        chunk.debugColor[2],
        chunk.debugColor[3]
    );

    return true;
}

bool ClusteredMeshCache::readGroup(std::ifstream& file, ClusterGroup& group) {
    ClusterGroupChunkHeader chunk{};
    file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
    if (!file.good()) return false;

    group.groupId = chunk.groupId;
    group.lodLevel = chunk.lodLevel;
    group.clusterStart = chunk.clusterStart;
    group.clusterCount = chunk.clusterCount;

    group.boundingSphereCenter = glm::vec3(
        chunk.boundingSphereCenter[0],
        chunk.boundingSphereCenter[1],
        chunk.boundingSphereCenter[2]
    );
    group.boundingSphereRadius = chunk.boundingSphereRadius;

    group.lodError = chunk.lodError;
    group.parentError = chunk.parentError;

    group.parentGroupStart = chunk.parentGroupStart;
    group.parentGroupCount = chunk.parentGroupCount;
    group.childGroupStart = chunk.childGroupStart;
    group.childGroupCount = chunk.childGroupCount;

    return true;
}

bool ClusteredMeshCache::readVertices(std::ifstream& file,
                                       std::vector<ClusterVertex>& vertices,
                                       uint32_t count) {
    if (count == 0) {
        vertices.clear();
        return true;
    }

    vertices.resize(count);
    file.read(reinterpret_cast<char*>(vertices.data()),
              count * sizeof(ClusterVertex));
    return file.good();
}

bool ClusteredMeshCache::readIndices(std::ifstream& file,
                                      std::vector<uint32_t>& indices,
                                      uint32_t count) {
    if (count == 0) {
        indices.clear();
        return true;
    }

    indices.resize(count);
    file.read(reinterpret_cast<char*>(indices.data()),
              count * sizeof(uint32_t));
    return file.good();
}

} // namespace MiEngine
