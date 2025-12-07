#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>

namespace MiEngine {

// ============================================================================
// Constants
// ============================================================================

constexpr uint32_t VGEO_MAX_CLUSTER_TRIANGLES = 128;      // Target triangles per cluster
constexpr uint32_t VGEO_MIN_CLUSTER_TRIANGLES = 64;       // Minimum for valid cluster
constexpr uint32_t VGEO_MAX_CLUSTER_VERTICES = 256;       // Max vertices per cluster
constexpr uint32_t VGEO_MAX_LOD_LEVELS = 16;              // Maximum LOD levels in DAG
constexpr float VGEO_SIMPLIFICATION_RATIO = 0.5f;         // Target 50% reduction per LOD
constexpr float VGEO_ERROR_THRESHOLD = 0.01f;             // Screen-space error threshold

// ============================================================================
// Cluster Vertex (compact for GPU)
// ============================================================================

struct ClusterVertex {
    glm::vec3 position;
    float pad0;
    glm::vec3 normal;
    float pad1;
    glm::vec2 texCoord;
    glm::vec2 pad2;
    // 48 bytes total, aligned for GPU
};

// ============================================================================
// Cluster - The fundamental unit of Virtual Geo rendering
// ============================================================================

struct Cluster {
    // Identification
    uint32_t clusterId;              // Unique cluster ID
    uint32_t lodLevel;               // 0 = highest detail, N = coarsest
    uint32_t meshId;                 // Parent mesh this cluster belongs to

    // Geometry offsets into global buffers
    uint32_t vertexOffset;           // Offset into global vertex buffer
    uint32_t vertexCount;            // Number of vertices in this cluster
    uint32_t indexOffset;            // Offset into global index buffer
    uint32_t triangleCount;          // Number of triangles (indexCount / 3)

    // Bounding volumes for culling
    glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;
    glm::vec3 aabbMin;
    float pad0;
    glm::vec3 aabbMax;
    float pad1;

    // LOD error metrics
    float lodError;                  // Geometric error of this cluster
    float parentError;               // Error of parent (for LOD selection)
    float screenSpaceError;          // Cached screen-space error (updated per frame)
    float maxChildError;             // Maximum error among all children

    // DAG relationships (indices into cluster array)
    uint32_t parentClusterStart;     // First parent cluster index
    uint32_t parentClusterCount;     // Number of parent clusters (usually 1-2)
    uint32_t childClusterStart;      // First child cluster index
    uint32_t childClusterCount;      // Number of child clusters

    // Material and rendering
    uint32_t materialIndex;          // Index into material array
    uint32_t flags;                  // Bit flags (see ClusterFlags)

    // Debug info
    glm::vec4 debugColor;            // For cluster visualization

    // Check if this is a leaf cluster (highest detail)
    bool isLeaf() const { return childClusterCount == 0; }

    // Check if this is a root cluster (lowest detail)
    bool isRoot() const { return parentClusterCount == 0; }
};

// Cluster flags
enum ClusterFlags : uint32_t {
    CLUSTER_FLAG_NONE = 0,
    CLUSTER_FLAG_VISIBLE = 1 << 0,           // Currently visible
    CLUSTER_FLAG_SELECTED = 1 << 1,          // Selected for rendering this frame
    CLUSTER_FLAG_STREAMING = 1 << 2,         // Being streamed in
    CLUSTER_FLAG_RESIDENT = 1 << 3,          // Fully resident in GPU memory
    CLUSTER_FLAG_CAST_SHADOW = 1 << 4,       // Casts shadows
    CLUSTER_FLAG_TWO_SIDED = 1 << 5,         // Two-sided rendering
};

// ============================================================================
// Cluster Group - Groups of clusters that share LOD transitions
// ============================================================================

struct ClusterGroup {
    uint32_t groupId;
    uint32_t lodLevel;

    // Clusters in this group
    uint32_t clusterStart;           // First cluster index
    uint32_t clusterCount;           // Number of clusters

    // Bounding volume for entire group
    glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;

    // LOD error for the entire group
    float lodError;
    float parentError;

    // Parent group(s) for LOD traversal
    uint32_t parentGroupStart;
    uint32_t parentGroupCount;

    // Child groups
    uint32_t childGroupStart;
    uint32_t childGroupCount;
};

// ============================================================================
// Clustered Mesh - Complete Virtual Geo-ready mesh data
// ============================================================================

struct ClusteredMesh {
    std::string name;
    uint32_t meshId;

    // All clusters across all LOD levels
    std::vector<Cluster> clusters;

    // Cluster groups (optional, for grouped LOD transitions)
    std::vector<ClusterGroup> groups;

    // Geometry data (to be uploaded to GPU)
    std::vector<ClusterVertex> vertices;
    std::vector<uint32_t> indices;

    // LOD hierarchy info
    uint32_t maxLodLevel;            // Highest LOD level (coarsest)
    uint32_t rootClusterStart;       // First root cluster (coarsest LOD)
    uint32_t rootClusterCount;       // Number of root clusters
    uint32_t leafClusterStart;       // First leaf cluster (finest LOD)
    uint32_t leafClusterCount;       // Number of leaf clusters

    // Total counts
    uint32_t totalTriangles;
    uint32_t totalVertices;

    // Bounding volume for entire mesh
    glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;

    // Error metrics
    float maxError;                  // Maximum error in the hierarchy
    float minError;                  // Minimum error (usually 0 for leaves)

    // Get clusters at a specific LOD level
    void getClustersAtLod(uint32_t lod, std::vector<uint32_t>& outIndices) const {
        outIndices.clear();
        for (uint32_t i = 0; i < clusters.size(); i++) {
            if (clusters[i].lodLevel == lod) {
                outIndices.push_back(i);
            }
        }
    }

    // Get cluster count at specific LOD
    uint32_t getClusterCountAtLod(uint32_t lod) const {
        uint32_t count = 0;
        for (const auto& c : clusters) {
            if (c.lodLevel == lod) count++;
        }
        return count;
    }

    // Get triangle count at specific LOD
    uint32_t getTriangleCountAtLod(uint32_t lod) const {
        uint32_t count = 0;
        for (const auto& c : clusters) {
            if (c.lodLevel == lod) count += c.triangleCount;
        }
        return count;
    }
};

// ============================================================================
// GPU Structures (for shader access)
// ============================================================================

// Cluster data for GPU (matches shader layout)
struct GPUClusterData {
    glm::vec4 boundingSphere;        // xyz = center, w = radius
    glm::vec4 aabbMin;               // xyz = min, w = lodError
    glm::vec4 aabbMax;               // xyz = max, w = parentError

    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t triangleCount;

    uint32_t lodLevel;
    uint32_t materialIndex;
    uint32_t flags;
    uint32_t padding;
};

// Instance data for GPU (per-instance transform + cluster selection)
struct GPUVirtualGeoInstance {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
    uint32_t meshId;                 // Which ClusteredMesh
    uint32_t firstCluster;           // Offset into global cluster array
    uint32_t clusterCount;           // Number of clusters in this mesh
    uint32_t flags;
};

// LOD selection uniforms
struct LODSelectionUniforms {
    glm::mat4 viewProj;
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float screenHeight;
    float fovY;
    float lodErrorThreshold;         // Screen-space error threshold in pixels
    uint32_t totalClusters;
    uint32_t frameNumber;
    uint32_t enableFrustumCull;
    uint32_t enableLODSelection;
    uint32_t debugMode;
};

// ============================================================================
// Mesh Clustering Statistics
// ============================================================================

struct ClusteringStats {
    uint32_t inputTriangles;
    uint32_t inputVertices;
    uint32_t outputClusters;
    uint32_t lodLevels;
    float averageClusterSize;        // Average triangles per cluster
    float clusteringTime;            // Time in milliseconds
    float dagBuildTime;              // Time to build DAG
    float totalTime;

    void print() const;
};

// ============================================================================
// Clustering Options
// ============================================================================

struct ClusteringOptions {
    uint32_t targetClusterSize = VGEO_MAX_CLUSTER_TRIANGLES;
    uint32_t minClusterSize = VGEO_MIN_CLUSTER_TRIANGLES;
    float simplificationRatio = VGEO_SIMPLIFICATION_RATIO;
    float errorThreshold = VGEO_ERROR_THRESHOLD;
    uint32_t maxLodLevels = VGEO_MAX_LOD_LEVELS;
    bool generateDebugColors = true;
    bool verbose = false;
};

} // namespace MiEngine
