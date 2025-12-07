#include "include/virtualgeo/ClusterDAGBuilder.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <cmath>

namespace MiEngine {

// ============================================================================
// QuadricMatrix Implementation
// ============================================================================

float QuadricMatrix::evaluate(const glm::vec3& v) const {
    // Q(v) = v^T * A * v = a00*x^2 + 2*a01*xy + 2*a02*xz + 2*a03*x
    //                    + a11*y^2 + 2*a12*yz + 2*a13*y
    //                    + a22*z^2 + 2*a23*z + a33
    float x = v.x, y = v.y, z = v.z;
    return a[0]*x*x + 2*a[1]*x*y + 2*a[2]*x*z + 2*a[3]*x
         + a[4]*y*y + 2*a[5]*y*z + 2*a[6]*y
         + a[7]*z*z + 2*a[8]*z + a[9];
}

QuadricMatrix QuadricMatrix::fromPlane(float px, float py, float pz, float pw) {
    QuadricMatrix q;
    // Q = p * p^T where p = (a, b, c, d)
    q.a[0] = px * px;
    q.a[1] = px * py;
    q.a[2] = px * pz;
    q.a[3] = px * pw;
    q.a[4] = py * py;
    q.a[5] = py * pz;
    q.a[6] = py * pw;
    q.a[7] = pz * pz;
    q.a[8] = pz * pw;
    q.a[9] = pw * pw;
    return q;
}

QuadricMatrix QuadricMatrix::fromTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

    // Plane equation: n.x*x + n.y*y + n.z*z + d = 0
    float d = -glm::dot(normal, v0);
    return fromPlane(normal.x, normal.y, normal.z, d);
}

// ============================================================================
// ClusterDAGBuilder Implementation
// ============================================================================

ClusterDAGBuilder::ClusterDAGBuilder() {}

ClusterDAGBuilder::~ClusterDAGBuilder() {}

bool ClusterDAGBuilder::buildDAG(ClusteredMesh& mesh, const ClusteringOptions& options) {
    if (mesh.clusters.empty()) {
        std::cerr << "ClusterDAGBuilder: No clusters to build DAG from" << std::endl;
        return false;
    }

    if (options.verbose) {
        std::cout << "ClusterDAGBuilder: Building LOD hierarchy..." << std::endl;
        std::cout << "  Base level: " << mesh.clusters.size() << " clusters" << std::endl;
    }

    m_LODLevels = 1;
    m_TotalError = 0.0f;

    // Mark existing clusters as leaf clusters (LOD 0)
    mesh.leafClusterStart = 0;
    mesh.leafClusterCount = static_cast<uint32_t>(mesh.clusters.size());

    // Build successive LOD levels until we have a small number of root clusters
    uint32_t currentLevel = 0;
    uint32_t clustersAtLevel = mesh.leafClusterCount;

    while (clustersAtLevel > 2 && m_LODLevels < options.maxLodLevels) {
        uint32_t prevClusterCount = static_cast<uint32_t>(mesh.clusters.size());

        if (!buildLODLevel(mesh, currentLevel, currentLevel + 1, options)) {
            if (options.verbose) {
                std::cout << "  Could not build LOD " << (currentLevel + 1) << std::endl;
            }
            break;
        }

        uint32_t newClusters = static_cast<uint32_t>(mesh.clusters.size()) - prevClusterCount;
        clustersAtLevel = newClusters;
        currentLevel++;
        m_LODLevels++;

        if (options.verbose) {
            std::cout << "  LOD " << currentLevel << ": " << newClusters << " clusters" << std::endl;
        }
    }

    // Mark root clusters
    mesh.maxLodLevel = currentLevel;
    mesh.rootClusterStart = static_cast<uint32_t>(mesh.clusters.size()) - clustersAtLevel;
    mesh.rootClusterCount = clustersAtLevel;

    // Compute max/min error across hierarchy
    mesh.maxError = 0.0f;
    mesh.minError = FLT_MAX;
    for (const auto& c : mesh.clusters) {
        mesh.maxError = std::max(mesh.maxError, c.lodError);
        mesh.minError = std::min(mesh.minError, c.lodError);
    }

    if (options.verbose) {
        std::cout << "ClusterDAGBuilder: Built " << m_LODLevels << " LOD levels" << std::endl;
        std::cout << "  Total clusters: " << mesh.clusters.size() << std::endl;
        std::cout << "  Root clusters: " << mesh.rootClusterCount << std::endl;
        std::cout << "  Error range: " << mesh.minError << " - " << mesh.maxError << std::endl;
    }

    return true;
}

bool ClusterDAGBuilder::buildLODLevel(ClusteredMesh& mesh,
                                       uint32_t sourceLevel,
                                       uint32_t targetLevel,
                                       const ClusteringOptions& options) {
    // Find all clusters at source level
    std::vector<uint32_t> sourceClusters;
    for (uint32_t i = 0; i < mesh.clusters.size(); i++) {
        if (mesh.clusters[i].lodLevel == sourceLevel) {
            sourceClusters.push_back(i);
        }
    }

    if (sourceClusters.size() < 2) {
        return false;  // Need at least 2 clusters to merge
    }

    // Group adjacent clusters for merging
    std::vector<std::vector<uint32_t>> clusterGroups;
    mergeAdjacentClusters(mesh.clusters, clusterGroups);

    if (clusterGroups.empty()) {
        return false;
    }

    // Filter to only include groups from source level
    std::vector<std::vector<uint32_t>> levelGroups;
    for (const auto& group : clusterGroups) {
        std::vector<uint32_t> filtered;
        for (uint32_t idx : group) {
            if (mesh.clusters[idx].lodLevel == sourceLevel) {
                filtered.push_back(idx);
            }
        }
        if (filtered.size() >= 2) {
            levelGroups.push_back(filtered);
        }
    }

    if (levelGroups.empty()) {
        // If no multi-cluster groups, create pairs
        levelGroups.clear();
        for (size_t i = 0; i + 1 < sourceClusters.size(); i += 2) {
            levelGroups.push_back({sourceClusters[i], sourceClusters[i + 1]});
        }
    }

    // Create coarser clusters from each group
    uint32_t globalVertexOffset = static_cast<uint32_t>(mesh.vertices.size());
    uint32_t globalIndexOffset = static_cast<uint32_t>(mesh.indices.size());

    for (const auto& group : levelGroups) {
        // Simplify the group
        std::vector<ClusterVertex> simplifiedVerts;
        std::vector<uint32_t> simplifiedIndices;

        simplifyClusterGroup(group, mesh, simplifiedVerts, simplifiedIndices, options.simplificationRatio);

        if (simplifiedIndices.empty()) continue;

        // Create new cluster
        Cluster newCluster{};
        newCluster.clusterId = static_cast<uint32_t>(mesh.clusters.size());
        newCluster.lodLevel = targetLevel;
        newCluster.meshId = mesh.meshId;

        newCluster.vertexOffset = globalVertexOffset;
        newCluster.vertexCount = static_cast<uint32_t>(simplifiedVerts.size());
        newCluster.indexOffset = globalIndexOffset;
        newCluster.triangleCount = static_cast<uint32_t>(simplifiedIndices.size()) / 3;

        // Compute bounds
        updateClusterBounds(newCluster, simplifiedVerts, 0, newCluster.vertexCount);

        // Compute LOD error (sum of child errors + simplification error)
        float maxChildError = 0.0f;
        for (uint32_t childIdx : group) {
            maxChildError = std::max(maxChildError, mesh.clusters[childIdx].lodError);
        }
        newCluster.lodError = maxChildError + computeSimplificationError(mesh.vertices, simplifiedVerts);
        newCluster.parentError = newCluster.lodError;  // Will be updated by parent
        newCluster.maxChildError = maxChildError;

        // Link parent-child
        newCluster.childClusterStart = group[0];
        newCluster.childClusterCount = static_cast<uint32_t>(group.size());

        // Update children to point to this parent
        for (uint32_t childIdx : group) {
            mesh.clusters[childIdx].parentClusterStart = newCluster.clusterId;
            mesh.clusters[childIdx].parentClusterCount = 1;
            mesh.clusters[childIdx].parentError = newCluster.lodError;
        }

        newCluster.materialIndex = mesh.clusters[group[0]].materialIndex;
        newCluster.flags = CLUSTER_FLAG_RESIDENT;

        // Generate debug color
        float hue = std::fmod(newCluster.clusterId * 0.618033988749895f, 1.0f);
        float s = 0.5f + 0.3f * (targetLevel / 5.0f);  // Increase saturation with LOD
        float v = 0.9f - 0.1f * targetLevel;           // Decrease brightness with LOD
        // Simple HSV to RGB
        int i = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - i;
        float p = v * (1 - s);
        float q = v * (1 - f * s);
        float t = v * (1 - (1 - f) * s);
        glm::vec3 rgb;
        switch (i % 6) {
            case 0: rgb = glm::vec3(v, t, p); break;
            case 1: rgb = glm::vec3(q, v, p); break;
            case 2: rgb = glm::vec3(p, v, t); break;
            case 3: rgb = glm::vec3(p, q, v); break;
            case 4: rgb = glm::vec3(t, p, v); break;
            case 5: rgb = glm::vec3(v, p, q); break;
        }
        newCluster.debugColor = glm::vec4(rgb, 1.0f);

        // Append to mesh
        mesh.clusters.push_back(newCluster);

        for (const auto& v : simplifiedVerts) {
            mesh.vertices.push_back(v);
        }
        for (uint32_t idx : simplifiedIndices) {
            mesh.indices.push_back(idx);
        }

        globalVertexOffset += newCluster.vertexCount;
        globalIndexOffset += static_cast<uint32_t>(simplifiedIndices.size());
    }

    return true;
}

void ClusterDAGBuilder::simplifyClusterGroup(const std::vector<uint32_t>& sourceClusterIndices,
                                              const ClusteredMesh& mesh,
                                              std::vector<ClusterVertex>& outVertices,
                                              std::vector<uint32_t>& outIndices,
                                              float targetReduction) {
    // Combine all cluster geometry
    std::vector<ClusterVertex> combinedVerts;
    std::vector<uint32_t> combinedIndices;

    for (uint32_t clusterIdx : sourceClusterIndices) {
        const Cluster& c = mesh.clusters[clusterIdx];
        uint32_t baseVertex = static_cast<uint32_t>(combinedVerts.size());

        // Copy vertices
        for (uint32_t i = 0; i < c.vertexCount; i++) {
            combinedVerts.push_back(mesh.vertices[c.vertexOffset + i]);
        }

        // Copy and offset indices
        for (uint32_t i = 0; i < c.triangleCount * 3; i++) {
            combinedIndices.push_back(mesh.indices[c.indexOffset + i] + baseVertex);
        }
    }

    if (combinedIndices.empty()) {
        return;
    }

    // Target triangle count
    uint32_t sourceTriangles = static_cast<uint32_t>(combinedIndices.size()) / 3;
    uint32_t targetTriangles = static_cast<uint32_t>(sourceTriangles * targetReduction);
    targetTriangles = std::max(targetTriangles, VGEO_MIN_CLUSTER_TRIANGLES);

    // Simplify using edge collapse
    outVertices = combinedVerts;
    outIndices = combinedIndices;

    if (targetTriangles < sourceTriangles) {
        edgeCollapseSimplify(outVertices, outIndices, targetTriangles);
    }
}

void ClusterDAGBuilder::mergeAdjacentClusters(const std::vector<Cluster>& sourceClusters,
                                               std::vector<std::vector<uint32_t>>& clusterGroups) {
    // Simple spatial grouping based on bounding sphere overlap
    std::vector<bool> grouped(sourceClusters.size(), false);
    clusterGroups.clear();

    for (uint32_t i = 0; i < sourceClusters.size(); i++) {
        if (grouped[i]) continue;

        std::vector<uint32_t> group;
        group.push_back(i);
        grouped[i] = true;

        const Cluster& ci = sourceClusters[i];

        // Find adjacent clusters
        for (uint32_t j = i + 1; j < sourceClusters.size(); j++) {
            if (grouped[j]) continue;

            const Cluster& cj = sourceClusters[j];

            // Check if bounding spheres overlap or are close
            float distance = glm::length(ci.boundingSphereCenter - cj.boundingSphereCenter);
            float threshold = (ci.boundingSphereRadius + cj.boundingSphereRadius) * 1.5f;

            if (distance < threshold) {
                group.push_back(j);
                grouped[j] = true;

                // Limit group size
                if (group.size() >= 4) break;
            }
        }

        if (group.size() >= 2) {
            clusterGroups.push_back(group);
        }
    }
}

float ClusterDAGBuilder::computeSimplificationError(const std::vector<ClusterVertex>& original,
                                                     const std::vector<ClusterVertex>& simplified) {
    // Simple error metric: Hausdorff-like distance approximation
    if (original.empty() || simplified.empty()) return 0.0f;

    float maxError = 0.0f;

    // Sample a subset of original vertices and find distance to simplified mesh
    size_t sampleCount = std::min(original.size(), size_t(100));
    size_t step = std::max(size_t(1), original.size() / sampleCount);

    for (size_t i = 0; i < original.size(); i += step) {
        const glm::vec3& p = original[i].position;

        float minDist = FLT_MAX;
        for (const auto& sv : simplified) {
            float d = glm::length(p - sv.position);
            minDist = std::min(minDist, d);
        }

        maxError = std::max(maxError, minDist);
    }

    return maxError;
}

void ClusterDAGBuilder::edgeCollapseSimplify(std::vector<ClusterVertex>& vertices,
                                              std::vector<uint32_t>& indices,
                                              uint32_t targetTriangles) {
    // Build quadric error metrics
    std::vector<QuadricMatrix> quadrics;
    buildQuadrics(vertices, indices, quadrics);

    // Build edge list
    std::unordered_map<uint64_t, CollapseEdge> edges;
    auto makeEdgeKey = [](uint32_t v0, uint32_t v1) -> uint64_t {
        if (v0 > v1) std::swap(v0, v1);
        return (uint64_t(v0) << 32) | v1;
    };

    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;

    for (uint32_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];

        uint64_t keys[3] = {makeEdgeKey(i0, i1), makeEdgeKey(i1, i2), makeEdgeKey(i2, i0)};
        uint32_t pairs[3][2] = {{i0, i1}, {i1, i2}, {i2, i0}};

        for (int e = 0; e < 3; e++) {
            if (edges.find(keys[e]) == edges.end()) {
                CollapseEdge edge;
                edge.v0 = pairs[e][0];
                edge.v1 = pairs[e][1];

                QuadricMatrix combined = quadrics[edge.v0];
                combined.add(quadrics[edge.v1]);

                edge.targetPos = findOptimalPosition(combined, vertices[edge.v0].position, vertices[edge.v1].position);
                edge.cost = combined.evaluate(edge.targetPos);

                edges[keys[e]] = edge;
            }
        }
    }

    // Build priority queue
    std::priority_queue<CollapseEdge, std::vector<CollapseEdge>, std::greater<CollapseEdge>> pq;
    for (const auto& [key, edge] : edges) {
        pq.push(edge);
    }

    // Vertex remap (collapsed vertices point to their target)
    std::vector<uint32_t> vertexRemap(vertices.size());
    for (uint32_t i = 0; i < vertices.size(); i++) {
        vertexRemap[i] = i;
    }

    auto findRoot = [&vertexRemap](uint32_t v) -> uint32_t {
        while (vertexRemap[v] != v) {
            vertexRemap[v] = vertexRemap[vertexRemap[v]];  // Path compression
            v = vertexRemap[v];
        }
        return v;
    };

    // Collapse edges until we reach target
    uint32_t currentTriangles = numTriangles;

    while (currentTriangles > targetTriangles && !pq.empty()) {
        CollapseEdge edge = pq.top();
        pq.pop();

        uint32_t v0 = findRoot(edge.v0);
        uint32_t v1 = findRoot(edge.v1);

        if (v0 == v1) continue;  // Already collapsed

        // Collapse v1 into v0
        vertexRemap[v1] = v0;
        vertices[v0].position = edge.targetPos;
        vertices[v0].normal = glm::normalize(vertices[v0].normal + vertices[v1].normal);

        // Merge quadrics
        quadrics[v0].add(quadrics[v1]);

        currentTriangles -= 2;  // Approximate (actual depends on neighborhood)
    }

    // Rebuild indices with collapsed vertices
    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());

    for (uint32_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = findRoot(indices[t * 3 + 0]);
        uint32_t i1 = findRoot(indices[t * 3 + 1]);
        uint32_t i2 = findRoot(indices[t * 3 + 2]);

        // Skip degenerate triangles
        if (i0 != i1 && i1 != i2 && i2 != i0) {
            newIndices.push_back(i0);
            newIndices.push_back(i1);
            newIndices.push_back(i2);
        }
    }

    indices = std::move(newIndices);

    // Compact vertices (remove unreferenced)
    std::unordered_set<uint32_t> usedVerts(indices.begin(), indices.end());
    std::vector<ClusterVertex> compactVerts;
    std::unordered_map<uint32_t, uint32_t> oldToNew;

    for (uint32_t v : usedVerts) {
        oldToNew[v] = static_cast<uint32_t>(compactVerts.size());
        compactVerts.push_back(vertices[v]);
    }

    for (uint32_t& idx : indices) {
        idx = oldToNew[idx];
    }

    vertices = std::move(compactVerts);
}

void ClusterDAGBuilder::buildQuadrics(const std::vector<ClusterVertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       std::vector<QuadricMatrix>& quadrics) {
    quadrics.resize(vertices.size());

    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
    for (uint32_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];

        QuadricMatrix q = QuadricMatrix::fromTriangle(
            vertices[i0].position,
            vertices[i1].position,
            vertices[i2].position
        );

        quadrics[i0].add(q);
        quadrics[i1].add(q);
        quadrics[i2].add(q);
    }
}

glm::vec3 ClusterDAGBuilder::findOptimalPosition(const QuadricMatrix& q, const glm::vec3& v0, const glm::vec3& v1) {
    // For simplicity, use midpoint
    // Full QEM would solve a 3x3 linear system here
    glm::vec3 mid = (v0 + v1) * 0.5f;

    // Evaluate costs at endpoints and midpoint
    float cost0 = q.evaluate(v0);
    float cost1 = q.evaluate(v1);
    float costMid = q.evaluate(mid);

    if (cost0 <= cost1 && cost0 <= costMid) return v0;
    if (cost1 <= cost0 && cost1 <= costMid) return v1;
    return mid;
}

void ClusterDAGBuilder::updateClusterBounds(Cluster& cluster,
                                             const std::vector<ClusterVertex>& vertices,
                                             uint32_t vertexOffset,
                                             uint32_t vertexCount) {
    if (vertexCount == 0) return;

    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);
    glm::vec3 centroid(0.0f);

    for (uint32_t i = 0; i < vertexCount; i++) {
        const auto& v = vertices[vertexOffset + i];
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
        centroid += v.position;
    }

    centroid /= static_cast<float>(vertexCount);

    float maxRadiusSq = 0.0f;
    for (uint32_t i = 0; i < vertexCount; i++) {
        const auto& v = vertices[vertexOffset + i];
        float distSq = glm::dot(v.position - centroid, v.position - centroid);
        maxRadiusSq = std::max(maxRadiusSq, distSq);
    }

    cluster.boundingSphereCenter = centroid;
    cluster.boundingSphereRadius = std::sqrt(maxRadiusSq);
    cluster.aabbMin = minBounds;
    cluster.aabbMax = maxBounds;
}

void ClusterDAGBuilder::linkParentChild(Cluster& parent,
                                         const std::vector<uint32_t>& childIndices,
                                         ClusteredMesh& mesh) {
    parent.childClusterStart = childIndices[0];
    parent.childClusterCount = static_cast<uint32_t>(childIndices.size());

    for (uint32_t childIdx : childIndices) {
        Cluster& child = mesh.clusters[childIdx];
        child.parentClusterStart = parent.clusterId;
        child.parentClusterCount = 1;
        child.parentError = parent.lodError;
    }
}

} // namespace MiEngine
