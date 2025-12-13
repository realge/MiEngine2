#pragma once

#include "VirtualGeoTypes.h"
#include "include/Utils/CommonVertex.h"  // For Vertex struct
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Forward declaration
class Mesh;

namespace MiEngine {

// ============================================================================
// Triangle Adjacency Graph
// ============================================================================

struct TriangleAdjacency {
    std::vector<std::vector<uint32_t>> neighbors;  // neighbors[tri] = list of adjacent triangles

    // Build adjacency from index buffer (for indexed meshes with shared vertices)
    void build(const std::vector<uint32_t>& indices, uint32_t vertexCount);

    // Build adjacency from vertex positions (for non-indexed meshes or meshes with duplicate vertices)
    // This uses spatial hashing to find triangles that share edge positions
    void buildFromPositions(const std::vector<Vertex>& vertices,
                            const std::vector<uint32_t>& indices,
                            float positionTolerance = 0.0001f);

    void clear() { neighbors.clear(); }
};

// ============================================================================
// Mesh Clusterer - Partitions mesh into ~128 triangle clusters using METIS
// ============================================================================

class MeshClusterer {
public:
    MeshClusterer();
    ~MeshClusterer();

    // Main clustering entry point
    // Takes raw mesh data and produces clusters for LOD 0 (finest detail)
    bool clusterMesh(const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices,
                     const ClusteringOptions& options,
                     ClusteredMesh& outMesh);

    // Cluster using existing Mesh object
    bool clusterMesh(const ::Mesh& mesh,
                     const ClusteringOptions& options,
                     ClusteredMesh& outMesh);

    // Get statistics from last clustering operation
    const ClusteringStats& getStats() const { return m_Stats; }

    // Check if METIS is available (compiled with METIS support)
    static bool isMetisAvailable();

private:
    // Build triangle adjacency graph
    void buildAdjacencyGraph(const std::vector<uint32_t>& indices,
                             uint32_t vertexCount,
                             TriangleAdjacency& adjacency);

    // Partition triangles into clusters using METIS
    bool partitionWithMetis(const TriangleAdjacency& adjacency,
                            uint32_t numTriangles,
                            uint32_t targetClusterCount,
                            std::vector<uint32_t>& clusterAssignment);

    // Fallback greedy partitioning (if METIS unavailable)
    void partitionGreedy(const TriangleAdjacency& adjacency,
                         uint32_t numTriangles,
                         uint32_t targetClusterSize,
                         std::vector<uint32_t>& clusterAssignment);

    // Create Cluster objects from partition assignment
    void createClustersFromPartition(const std::vector<Vertex>& vertices,
                                     const std::vector<uint32_t>& indices,
                                     const std::vector<uint32_t>& clusterAssignment,
                                     uint32_t numClusters,
                                     const ClusteringOptions& options,
                                     ClusteredMesh& outMesh);

    // Compute bounding volumes for a cluster
    void computeClusterBounds(const std::vector<ClusterVertex>& vertices,
                              uint32_t vertexOffset,
                              uint32_t vertexCount,
                              Cluster& cluster);

    // Compute mesh-wide bounding volumes
    void computeMeshBounds(ClusteredMesh& mesh);

    // Generate debug colors for clusters
    glm::vec4 generateDebugColor(uint32_t clusterId);

    // Remap vertices to be cluster-local (deduplication)
    void remapClusterVertices(const std::vector<Vertex>& srcVertices,
                              const std::vector<uint32_t>& srcIndices,
                              const std::vector<uint32_t>& triangleList,
                              std::vector<ClusterVertex>& outVertices,
                              std::vector<uint32_t>& outIndices);

    ClusteringStats m_Stats;
};

} // namespace MiEngine
