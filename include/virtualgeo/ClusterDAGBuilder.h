#pragma once

#include "VirtualGeoTypes.h"
#include <vector>
#include <functional>

namespace MiEngine {

// ============================================================================
// Quadric Error Metrics (QEM) for mesh simplification
// ============================================================================

struct QuadricMatrix {
    // 4x4 symmetric matrix stored as 10 floats
    float a[10];  // a00, a01, a02, a03, a11, a12, a13, a22, a23, a33

    QuadricMatrix() { clear(); }

    void clear() {
        for (int i = 0; i < 10; i++) a[i] = 0.0f;
    }

    void add(const QuadricMatrix& other) {
        for (int i = 0; i < 10; i++) a[i] += other.a[i];
    }

    // Compute error for a vertex position
    float evaluate(const glm::vec3& v) const;

    // Create quadric from a plane (ax + by + cz + d = 0)
    static QuadricMatrix fromPlane(float px, float py, float pz, float pw);

    // Create quadric from triangle
    static QuadricMatrix fromTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);
};

// Edge for collapse operations
struct CollapseEdge {
    uint32_t v0, v1;           // Vertex indices
    glm::vec3 targetPos;       // Optimal collapse position
    float cost;                // Collapse cost (error)

    bool operator>(const CollapseEdge& other) const {
        return cost > other.cost;  // For min-heap
    }
};

// ============================================================================
// Cluster DAG Builder - Builds LOD hierarchy from clustered mesh
// ============================================================================

class ClusterDAGBuilder {
public:
    ClusterDAGBuilder();
    ~ClusterDAGBuilder();

    // Build DAG from base (LOD 0) clusters
    // Creates coarser LOD levels by simplifying and merging clusters
    bool buildDAG(ClusteredMesh& mesh, const ClusteringOptions& options);

    // Get statistics
    float getSimplificationError() const { return m_TotalError; }
    uint32_t getLODLevels() const { return m_LODLevels; }

private:
    // Build a single LOD level from the previous (finer) level
    bool buildLODLevel(ClusteredMesh& mesh,
                       uint32_t sourceLevel,
                       uint32_t targetLevel,
                       const ClusteringOptions& options);

    // Simplify a group of clusters into coarser clusters
    void simplifyClusterGroup(const std::vector<uint32_t>& sourceClusterIndices,
                              const ClusteredMesh& mesh,
                              std::vector<ClusterVertex>& outVertices,
                              std::vector<uint32_t>& outIndices,
                              float targetReduction);

    // Merge adjacent clusters for LOD
    void mergeAdjacentClusters(const std::vector<Cluster>& sourceClusters,
                               std::vector<std::vector<uint32_t>>& clusterGroups);

    // Compute LOD error for simplified geometry
    float computeSimplificationError(const std::vector<ClusterVertex>& original,
                                      const std::vector<ClusterVertex>& simplified);

    // Edge collapse simplification
    void edgeCollapseSimplify(std::vector<ClusterVertex>& vertices,
                              std::vector<uint32_t>& indices,
                              uint32_t targetTriangles);

    // Build quadric error metrics for vertices
    void buildQuadrics(const std::vector<ClusterVertex>& vertices,
                       const std::vector<uint32_t>& indices,
                       std::vector<QuadricMatrix>& quadrics);

    // Find optimal collapse position
    glm::vec3 findOptimalPosition(const QuadricMatrix& q, const glm::vec3& v0, const glm::vec3& v1);

    // Update cluster bounds after simplification
    void updateClusterBounds(Cluster& cluster,
                             const std::vector<ClusterVertex>& vertices,
                             uint32_t vertexOffset,
                             uint32_t vertexCount);

    // Connect parent-child relationships
    void linkParentChild(Cluster& parent,
                         const std::vector<uint32_t>& childIndices,
                         ClusteredMesh& mesh);

    float m_TotalError = 0.0f;
    uint32_t m_LODLevels = 0;
};

} // namespace MiEngine
