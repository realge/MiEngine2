#pragma once

#include "VirtualGeoTypes.h"
#include <filesystem>
#include <string>
#include <cstdint>

namespace fs = std::filesystem;

namespace MiEngine {

// ============================================================================
// Binary Cache Format Headers
// ============================================================================

#pragma pack(push, 1)

// Main file header (80 bytes)
struct ClusteredMeshCacheHeader {
    char magic[8];                  // "MICLUST1"
    uint32_t version;               // Format version
    uint32_t flags;                 // Reserved flags

    // Source tracking for cache invalidation
    uint64_t sourceFileHash;        // Hash of source file path
    uint64_t sourceModTime;         // Source file modification time

    // Cluster data
    uint32_t clusterCount;          // Total clusters across all LODs
    uint32_t groupCount;            // Number of cluster groups
    uint32_t maxLodLevel;           // Highest LOD level

    // Geometry totals
    uint32_t totalVertices;         // Total vertices in mesh
    uint32_t totalIndices;          // Total indices in mesh
    uint32_t totalTriangles;        // Total triangles

    // Hierarchy info
    uint32_t rootClusterStart;
    uint32_t rootClusterCount;
    uint32_t leafClusterStart;
    uint32_t leafClusterCount;

    // Bounding volume
    float boundingSphereCenter[3];
    float boundingSphereRadius;
    float aabbMin[3];
    float aabbMax[3];

    // Error metrics
    float maxError;
    float minError;

    // Reserved for future use
    uint32_t reserved[2];
};

// Per-cluster header in file
struct ClusterChunkHeader {
    uint32_t clusterId;
    uint32_t lodLevel;
    uint32_t meshId;

    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t triangleCount;

    float boundingSphereCenter[3];
    float boundingSphereRadius;
    float aabbMin[3];
    float aabbMax[3];

    float lodError;
    float parentError;
    float screenSpaceError;
    float maxChildError;

    uint32_t parentClusterStart;
    uint32_t parentClusterCount;
    uint32_t childClusterStart;
    uint32_t childClusterCount;

    uint32_t materialIndex;
    uint32_t flags;

    float debugColor[4];
};

// Per-group header (optional)
struct ClusterGroupChunkHeader {
    uint32_t groupId;
    uint32_t lodLevel;
    uint32_t clusterStart;
    uint32_t clusterCount;

    float boundingSphereCenter[3];
    float boundingSphereRadius;

    float lodError;
    float parentError;

    uint32_t parentGroupStart;
    uint32_t parentGroupCount;
    uint32_t childGroupStart;
    uint32_t childGroupCount;
};

#pragma pack(pop)

// ============================================================================
// ClusteredMeshCache - Binary serialization for clustered meshes
// ============================================================================

/**
 * ClusteredMeshCache handles binary serialization of ClusteredMesh data.
 *
 * File format (.micluster):
 *   - ClusteredMeshCacheHeader (80 bytes)
 *   - Mesh name (length-prefixed string)
 *   - ClusterChunkHeader[] (one per cluster)
 *   - ClusterGroupChunkHeader[] (one per group, if any)
 *   - ClusterVertex[] (all vertices)
 *   - uint32_t[] (all indices)
 *
 * Benefits:
 *   - Fast loading (no mesh processing needed)
 *   - Cache invalidation based on source file changes
 *   - Compact binary format
 */
class ClusteredMeshCache {
public:
    static constexpr char MAGIC[] = "MICLUST1";
    static constexpr uint32_t VERSION = 1;
    static constexpr const char* EXTENSION = ".micluster";

    // ========================================================================
    // Save/Load Operations
    // ========================================================================

    /**
     * Save a ClusteredMesh to a binary cache file.
     *
     * @param cachePath Path to output .micluster file
     * @param mesh The clustered mesh data to save
     * @param sourcePath Original source file path (for cache invalidation)
     * @return true if save succeeded
     */
    static bool save(const fs::path& cachePath,
                     const ClusteredMesh& mesh,
                     const fs::path& sourcePath);

    /**
     * Load a ClusteredMesh from a binary cache file.
     *
     * @param cachePath Path to .micluster file
     * @param outMesh Output clustered mesh data
     * @return true if load succeeded
     */
    static bool load(const fs::path& cachePath,
                     ClusteredMesh& outMesh);

    // ========================================================================
    // Cache Validation
    // ========================================================================

    /**
     * Check if a cache file is valid and up-to-date.
     *
     * @param cachePath Path to .micluster file
     * @param sourcePath Original source file path
     * @return true if cache is valid and newer than source
     */
    static bool isValid(const fs::path& cachePath,
                        const fs::path& sourcePath);

    /**
     * Check if a cache file exists and has valid header.
     * Does not check source file timestamp.
     */
    static bool exists(const fs::path& cachePath);

    // ========================================================================
    // Path Utilities
    // ========================================================================

    /**
     * Generate cache file path from source path.
     *
     * Example: "Models/robot.fbx" -> "Cache/robot_abc123.micluster"
     *
     * @param sourcePath Path to source model file
     * @param cacheDir Directory for cache files
     * @return Generated cache file path
     */
    static fs::path getCachePath(const fs::path& sourcePath,
                                 const fs::path& cacheDir);

    /**
     * Compute hash of source file path for cache naming.
     */
    static uint64_t computeSourceHash(const fs::path& sourcePath);

    /**
     * Get source file modification time.
     */
    static uint64_t getSourceModTime(const fs::path& sourcePath);

    // ========================================================================
    // Debug/Info
    // ========================================================================

    /**
     * Print cache file info to console.
     */
    static void printInfo(const fs::path& cachePath);

private:
    // Write helpers
    static bool writeHeader(std::ofstream& file,
                           const ClusteredMeshCacheHeader& header);
    static bool writeString(std::ofstream& file, const std::string& str);
    static bool writeCluster(std::ofstream& file, const Cluster& cluster);
    static bool writeGroup(std::ofstream& file, const ClusterGroup& group);
    static bool writeVertices(std::ofstream& file,
                             const std::vector<ClusterVertex>& vertices);
    static bool writeIndices(std::ofstream& file,
                            const std::vector<uint32_t>& indices);

    // Read helpers
    static bool readHeader(std::ifstream& file,
                          ClusteredMeshCacheHeader& header);
    static bool readString(std::ifstream& file, std::string& str);
    static bool readCluster(std::ifstream& file, Cluster& cluster);
    static bool readGroup(std::ifstream& file, ClusterGroup& group);
    static bool readVertices(std::ifstream& file,
                            std::vector<ClusterVertex>& vertices,
                            uint32_t count);
    static bool readIndices(std::ifstream& file,
                           std::vector<uint32_t>& indices,
                           uint32_t count);
};

} // namespace MiEngine
