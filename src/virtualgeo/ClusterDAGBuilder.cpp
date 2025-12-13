#include "include/virtualgeo/ClusterDAGBuilder.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <iostream>
#include <cmath>
#include <functional>

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

    // Store LOD 0 cluster indices for building all higher LODs
    std::vector<uint32_t> lod0Clusters;
    for (uint32_t i = 0; i < mesh.leafClusterCount; i++) {
        lod0Clusters.push_back(i);
    }

    // Build successive LOD levels - ALWAYS from LOD 0 with increasing simplification
    uint32_t currentLevel = 0;
    float cumulativeReduction = options.simplificationRatio;  // Start with 50%

    while (m_LODLevels < options.maxLodLevels) {
        uint32_t prevClusterCount = static_cast<uint32_t>(mesh.clusters.size());

        // Build next LOD from LOD 0 with cumulative reduction
        if (!buildLODLevelFromBase(mesh, lod0Clusters, currentLevel + 1, cumulativeReduction, options)) {
            if (options.verbose) {
                std::cout << "  Could not build LOD " << (currentLevel + 1) << std::endl;
            }
            break;
        }

        uint32_t newClusters = static_cast<uint32_t>(mesh.clusters.size()) - prevClusterCount;

        if (newClusters == 0) break;

        currentLevel++;
        m_LODLevels++;

        if (options.verbose) {
            uint32_t triCount = mesh.getTriangleCountAtLod(currentLevel);
            std::cout << "  LOD " << currentLevel << ": " << newClusters << " clusters, "
                      << triCount << " triangles (reduction=" << cumulativeReduction << ")" << std::endl;
        }

        // Increase reduction for next level
        cumulativeReduction *= options.simplificationRatio;

        // Stop if we're down to 1 cluster or very few triangles
        if (newClusters <= 1) break;
    }

    // Mark root clusters
    mesh.maxLodLevel = currentLevel;

    // Find root clusters (highest LOD level)
    uint32_t rootCount = 0;
    uint32_t rootStart = 0;
    for (uint32_t i = 0; i < mesh.clusters.size(); i++) {
        if (mesh.clusters[i].lodLevel == mesh.maxLodLevel) {
            if (rootCount == 0) rootStart = i;
            rootCount++;
        }
    }
    mesh.rootClusterStart = rootStart;
    mesh.rootClusterCount = rootCount;

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

// Build LOD level directly from LOD 0 base clusters with specified reduction
bool ClusterDAGBuilder::buildLODLevelFromBase(ClusteredMesh& mesh,
                                               const std::vector<uint32_t>& baseClusters,
                                               uint32_t targetLevel,
                                               float reductionRatio,
                                               const ClusteringOptions& options) {
    if (baseClusters.empty()) {
        return false;
    }

    // Combine ALL base clusters and simplify to target reduction
    std::vector<ClusterVertex> simplifiedVerts;
    std::vector<uint32_t> simplifiedIndices;

    // Temporarily modify the simplification to use our reduction ratio
    simplifyClusterGroupWithRatio(baseClusters, mesh, simplifiedVerts, simplifiedIndices, reductionRatio);

    if (simplifiedIndices.empty()) {
        return false;
    }

    uint32_t simplifiedTriCount = static_cast<uint32_t>(simplifiedIndices.size()) / 3;

    // Determine number of clusters for this LOD level
    // Best practice: maintain consistent cluster sizes across LOD levels
    // Each cluster should have roughly targetClusterSize triangles (default ~128)
    // This ensures efficient GPU rendering at all LOD levels

    uint32_t targetTrisPerCluster = options.targetClusterSize > 0 ? options.targetClusterSize : 128;

    // Calculate clusters needed to maintain target cluster size
    uint32_t targetNumClusters = (simplifiedTriCount + targetTrisPerCluster - 1) / targetTrisPerCluster;
    targetNumClusters = std::max(1u, targetNumClusters);

    // Cap at half the clusters from the previous level to ensure hierarchy progression
    uint32_t maxFromPrevLevel = std::max(1u, static_cast<uint32_t>(baseClusters.size()) / 2);
    targetNumClusters = std::min(targetNumClusters, maxFromPrevLevel);

    // Stop building more LOD levels when we reach a single cluster
    if (simplifiedTriCount <= targetTrisPerCluster) {
        targetNumClusters = 1;
    }

    if (options.verbose) {
        std::cout << "    LOD " << targetLevel << ": " << simplifiedTriCount << " tris -> "
                  << targetNumClusters << " clusters (target " << targetTrisPerCluster << " tris/cluster)" << std::endl;
    }

    uint32_t globalVertexOffset = static_cast<uint32_t>(mesh.vertices.size());
    uint32_t globalIndexOffset = static_cast<uint32_t>(mesh.indices.size());

    if (targetNumClusters == 1) {
        // Single cluster containing all simplified geometry
        Cluster newCluster{};
        newCluster.clusterId = static_cast<uint32_t>(mesh.clusters.size());
        newCluster.lodLevel = targetLevel;
        newCluster.meshId = mesh.meshId;
        newCluster.vertexOffset = globalVertexOffset;
        newCluster.vertexCount = static_cast<uint32_t>(simplifiedVerts.size());
        newCluster.indexOffset = globalIndexOffset;
        newCluster.triangleCount = simplifiedTriCount;

        updateClusterBounds(newCluster, simplifiedVerts, 0, newCluster.vertexCount);

        newCluster.lodError = 0.01f * targetLevel;
        newCluster.parentError = newCluster.lodError;
        newCluster.maxChildError = 0.0f;
        newCluster.childClusterStart = baseClusters[0];
        newCluster.childClusterCount = static_cast<uint32_t>(baseClusters.size());
        newCluster.materialIndex = mesh.clusters[baseClusters[0]].materialIndex;
        newCluster.flags = CLUSTER_FLAG_RESIDENT;

        // Generate debug color
        float hue = std::fmod(newCluster.clusterId * 0.618033988749895f, 1.0f);
        float s = 0.5f + 0.3f * (targetLevel / 5.0f);
        float v = 0.9f - 0.1f * targetLevel;
        int hi = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - hi;
        float p = v * (1 - s);
        float q = v * (1 - f * s);
        float t = v * (1 - (1 - f) * s);
        glm::vec3 rgb;
        switch (hi % 6) {
            case 0: rgb = glm::vec3(v, t, p); break;
            case 1: rgb = glm::vec3(q, v, p); break;
            case 2: rgb = glm::vec3(p, v, t); break;
            case 3: rgb = glm::vec3(p, q, v); break;
            case 4: rgb = glm::vec3(t, p, v); break;
            case 5: rgb = glm::vec3(v, p, q); break;
        }
        newCluster.debugColor = glm::vec4(rgb, 1.0f);

        mesh.clusters.push_back(newCluster);

        for (const auto& vert : simplifiedVerts) {
            mesh.vertices.push_back(vert);
        }
        for (uint32_t idx : simplifiedIndices) {
            mesh.indices.push_back(idx);
        }
    } else {
        // Multiple clusters: partition using k-d tree style recursive splitting
        // This creates compact rectangular regions instead of diagonal bands

        // Compute triangle centroids
        struct TriInfo {
            uint32_t triIdx;
            glm::vec3 centroid;
        };
        std::vector<TriInfo> triInfos(simplifiedTriCount);

        for (uint32_t t = 0; t < simplifiedTriCount; t++) {
            uint32_t i0 = simplifiedIndices[t * 3 + 0];
            uint32_t i1 = simplifiedIndices[t * 3 + 1];
            uint32_t i2 = simplifiedIndices[t * 3 + 2];

            triInfos[t].triIdx = t;
            triInfos[t].centroid = (simplifiedVerts[i0].position +
                                    simplifiedVerts[i1].position +
                                    simplifiedVerts[i2].position) / 3.0f;
        }

        // Recursive k-d tree partitioning function
        // Splits triangles into targetNumClusters groups by recursively splitting along longest axis
        std::vector<std::vector<uint32_t>> clusterTriLists(targetNumClusters);

        std::function<void(std::vector<uint32_t>&, uint32_t, uint32_t)> kdPartition;
        kdPartition = [&](std::vector<uint32_t>& triIndices, uint32_t startCluster, uint32_t numClusters) {
            if (numClusters <= 1 || triIndices.empty()) {
                // Base case: assign all triangles to startCluster
                for (uint32_t ti : triIndices) {
                    clusterTriLists[startCluster].push_back(ti);
                }
                return;
            }

            // Find bounding box of centroids
            glm::vec3 minBounds(FLT_MAX);
            glm::vec3 maxBounds(-FLT_MAX);
            for (uint32_t ti : triIndices) {
                minBounds = glm::min(minBounds, triInfos[ti].centroid);
                maxBounds = glm::max(maxBounds, triInfos[ti].centroid);
            }

            // Find longest axis
            glm::vec3 extent = maxBounds - minBounds;
            int axis = 0;
            if (extent.y > extent.x && extent.y > extent.z) axis = 1;
            else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

            // Sort triangles along longest axis
            std::sort(triIndices.begin(), triIndices.end(), [&](uint32_t a, uint32_t b) {
                return triInfos[a].centroid[axis] < triInfos[b].centroid[axis];
            });

            // Split into two halves
            uint32_t midPoint = static_cast<uint32_t>(triIndices.size()) / 2;
            uint32_t leftClusters = numClusters / 2;
            uint32_t rightClusters = numClusters - leftClusters;

            std::vector<uint32_t> leftTris(triIndices.begin(), triIndices.begin() + midPoint);
            std::vector<uint32_t> rightTris(triIndices.begin() + midPoint, triIndices.end());

            // Recurse
            kdPartition(leftTris, startCluster, leftClusters);
            kdPartition(rightTris, startCluster + leftClusters, rightClusters);
        };

        // Start partitioning
        std::vector<uint32_t> allTriIndices(simplifiedTriCount);
        for (uint32_t t = 0; t < simplifiedTriCount; t++) {
            allTriIndices[t] = t;
        }
        kdPartition(allTriIndices, 0, targetNumClusters);

        // Create clusters from partition results
        for (uint32_t c = 0; c < targetNumClusters; c++) {
            const auto& triList = clusterTriLists[c];
            if (triList.empty()) continue;

            std::vector<ClusterVertex> clusterVerts;
            std::vector<uint32_t> clusterIndices;
            std::unordered_map<uint32_t, uint32_t> vertRemap;

            for (uint32_t triLocalIdx : triList) {
                uint32_t origTri = triInfos[triLocalIdx].triIdx;
                for (int v = 0; v < 3; v++) {
                    uint32_t srcIdx = simplifiedIndices[origTri * 3 + v];
                    auto it = vertRemap.find(srcIdx);
                    if (it == vertRemap.end()) {
                        uint32_t newIdx = static_cast<uint32_t>(clusterVerts.size());
                        clusterVerts.push_back(simplifiedVerts[srcIdx]);
                        vertRemap[srcIdx] = newIdx;
                        clusterIndices.push_back(newIdx);
                    } else {
                        clusterIndices.push_back(it->second);
                    }
                }
            }

            if (clusterIndices.empty()) continue;

            Cluster newCluster{};
            newCluster.clusterId = static_cast<uint32_t>(mesh.clusters.size());
            newCluster.lodLevel = targetLevel;
            newCluster.meshId = mesh.meshId;
            newCluster.vertexOffset = globalVertexOffset;
            newCluster.vertexCount = static_cast<uint32_t>(clusterVerts.size());
            newCluster.indexOffset = globalIndexOffset;
            newCluster.triangleCount = static_cast<uint32_t>(clusterIndices.size()) / 3;

            updateClusterBounds(newCluster, clusterVerts, 0, newCluster.vertexCount);

            newCluster.lodError = 0.01f * targetLevel;
            newCluster.parentError = newCluster.lodError;
            newCluster.maxChildError = 0.0f;
            newCluster.childClusterStart = baseClusters[0];
            newCluster.childClusterCount = static_cast<uint32_t>(baseClusters.size());
            newCluster.materialIndex = mesh.clusters[baseClusters[0]].materialIndex;
            newCluster.flags = CLUSTER_FLAG_RESIDENT;

            float hue = std::fmod(newCluster.clusterId * 0.618033988749895f, 1.0f);
            float s = 0.5f + 0.3f * (targetLevel / 5.0f);
            float vb = 0.9f - 0.1f * targetLevel;
            int hi = static_cast<int>(hue * 6.0f);
            float f = hue * 6.0f - hi;
            float p = vb * (1 - s);
            float q = vb * (1 - f * s);
            float t = vb * (1 - (1 - f) * s);
            glm::vec3 rgb;
            switch (hi % 6) {
                case 0: rgb = glm::vec3(vb, t, p); break;
                case 1: rgb = glm::vec3(q, vb, p); break;
                case 2: rgb = glm::vec3(p, vb, t); break;
                case 3: rgb = glm::vec3(p, q, vb); break;
                case 4: rgb = glm::vec3(t, p, vb); break;
                case 5: rgb = glm::vec3(vb, p, q); break;
            }
            newCluster.debugColor = glm::vec4(rgb, 1.0f);

            mesh.clusters.push_back(newCluster);

            for (const auto& vert : clusterVerts) {
                mesh.vertices.push_back(vert);
            }
            for (uint32_t idx : clusterIndices) {
                mesh.indices.push_back(idx);
            }

            globalVertexOffset += newCluster.vertexCount;
            globalIndexOffset += static_cast<uint32_t>(clusterIndices.size());
        }
    }

    return true;
}

// Helper to simplify with a specific reduction ratio
void ClusterDAGBuilder::simplifyClusterGroupWithRatio(const std::vector<uint32_t>& sourceClusterIndices,
                                                       const ClusteredMesh& mesh,
                                                       std::vector<ClusterVertex>& outVertices,
                                                       std::vector<uint32_t>& outIndices,
                                                       float reductionRatio) {
    // Combine clusters with vertex welding
    std::vector<ClusterVertex> combinedVerts;
    std::vector<uint32_t> combinedIndices;

    auto positionHash = [](const glm::vec3& p) -> uint64_t {
        int32_t x = static_cast<int32_t>(p.x * 1000.0f);
        int32_t y = static_cast<int32_t>(p.y * 1000.0f);
        int32_t z = static_cast<int32_t>(p.z * 1000.0f);
        return (uint64_t(x & 0xFFFFF) << 40) | (uint64_t(y & 0xFFFFF) << 20) | uint64_t(z & 0xFFFFF);
    };

    std::unordered_map<uint64_t, uint32_t> positionToIndex;

    for (uint32_t clusterIdx : sourceClusterIndices) {
        const Cluster& c = mesh.clusters[clusterIdx];
        std::vector<uint32_t> localToGlobal(c.vertexCount);

        for (uint32_t i = 0; i < c.vertexCount; i++) {
            const ClusterVertex& v = mesh.vertices[c.vertexOffset + i];
            uint64_t hash = positionHash(v.position);

            auto it = positionToIndex.find(hash);
            if (it != positionToIndex.end()) {
                localToGlobal[i] = it->second;
            } else {
                uint32_t newIdx = static_cast<uint32_t>(combinedVerts.size());
                combinedVerts.push_back(v);
                positionToIndex[hash] = newIdx;
                localToGlobal[i] = newIdx;
            }
        }

        for (uint32_t i = 0; i < c.triangleCount * 3; i++) {
            uint32_t localIdx = mesh.indices[c.indexOffset + i];
            combinedIndices.push_back(localToGlobal[localIdx]);
        }
    }

    if (combinedIndices.empty()) {
        return;
    }

    uint32_t sourceTriangles = static_cast<uint32_t>(combinedIndices.size()) / 3;
    uint32_t targetTriangles = static_cast<uint32_t>(sourceTriangles * reductionRatio);
    targetTriangles = std::max(targetTriangles, VGEO_MIN_CLUSTER_TRIANGLES);

    outVertices = combinedVerts;
    outIndices = combinedIndices;

    if (targetTriangles < sourceTriangles) {
        edgeCollapseSimplify(outVertices, outIndices, targetTriangles);
    }
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

    // UNIFIED LOD APPROACH:
    // 1. Combine ALL source clusters into one mesh (with vertex welding)
    // 2. Simplify to target triangle count
    // 3. Partition simplified mesh spatially into N clusters
    // This ensures complete coverage with no holes

    // Step 1 & 2: Combine and simplify ALL source clusters
    std::vector<ClusterVertex> simplifiedVerts;
    std::vector<uint32_t> simplifiedIndices;

    simplifyClusterGroup(sourceClusters, mesh, simplifiedVerts, simplifiedIndices, options.simplificationRatio);

    if (simplifiedIndices.empty()) {
        return false;
    }

    uint32_t simplifiedTriCount = static_cast<uint32_t>(simplifiedIndices.size()) / 3;

    // Target: reduce to ~1/4 the number of clusters (merge 4 into 1)
    uint32_t targetNumClusters = std::max(1u, static_cast<uint32_t>(sourceClusters.size()) / 4);

    // If we only have a few clusters, just merge all into one
    if (sourceClusters.size() <= 4 || simplifiedTriCount < options.targetClusterSize * 2) {
        targetNumClusters = 1;
    }

    // Step 3: Partition simplified triangles spatially
    // Use triangle centroid to assign each triangle to a spatial region

    uint32_t globalVertexOffset = static_cast<uint32_t>(mesh.vertices.size());
    uint32_t globalIndexOffset = static_cast<uint32_t>(mesh.indices.size());

    if (targetNumClusters == 1) {
        // Single cluster containing all simplified geometry
        Cluster newCluster{};
        newCluster.clusterId = static_cast<uint32_t>(mesh.clusters.size());
        newCluster.lodLevel = targetLevel;
        newCluster.meshId = mesh.meshId;
        newCluster.vertexOffset = globalVertexOffset;
        newCluster.vertexCount = static_cast<uint32_t>(simplifiedVerts.size());
        newCluster.indexOffset = globalIndexOffset;
        newCluster.triangleCount = simplifiedTriCount;

        updateClusterBounds(newCluster, simplifiedVerts, 0, newCluster.vertexCount);

        float maxChildError = 0.0f;
        for (uint32_t childIdx : sourceClusters) {
            maxChildError = std::max(maxChildError, mesh.clusters[childIdx].lodError);
        }
        newCluster.lodError = maxChildError + 0.01f * targetLevel;
        newCluster.parentError = newCluster.lodError;
        newCluster.maxChildError = maxChildError;
        newCluster.childClusterStart = sourceClusters[0];
        newCluster.childClusterCount = static_cast<uint32_t>(sourceClusters.size());

        for (uint32_t childIdx : sourceClusters) {
            mesh.clusters[childIdx].parentClusterStart = newCluster.clusterId;
            mesh.clusters[childIdx].parentClusterCount = 1;
            mesh.clusters[childIdx].parentError = newCluster.lodError;
        }

        newCluster.materialIndex = mesh.clusters[sourceClusters[0]].materialIndex;
        newCluster.flags = CLUSTER_FLAG_RESIDENT;

        // Generate debug color
        float hue = std::fmod(newCluster.clusterId * 0.618033988749895f, 1.0f);
        float s = 0.5f + 0.3f * (targetLevel / 5.0f);
        float v = 0.9f - 0.1f * targetLevel;
        int hi = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - hi;
        float p = v * (1 - s);
        float q = v * (1 - f * s);
        float t = v * (1 - (1 - f) * s);
        glm::vec3 rgb;
        switch (hi % 6) {
            case 0: rgb = glm::vec3(v, t, p); break;
            case 1: rgb = glm::vec3(q, v, p); break;
            case 2: rgb = glm::vec3(p, v, t); break;
            case 3: rgb = glm::vec3(p, q, v); break;
            case 4: rgb = glm::vec3(t, p, v); break;
            case 5: rgb = glm::vec3(v, p, q); break;
        }
        newCluster.debugColor = glm::vec4(rgb, 1.0f);

        mesh.clusters.push_back(newCluster);

        for (const auto& v : simplifiedVerts) {
            mesh.vertices.push_back(v);
        }
        for (uint32_t idx : simplifiedIndices) {
            mesh.indices.push_back(idx);
        }
    } else {
        // Multiple clusters: partition using k-d tree style recursive splitting
        // This creates compact rectangular regions instead of diagonal bands

        // Compute triangle centroids
        struct TriInfo {
            uint32_t triIdx;
            glm::vec3 centroid;
        };
        std::vector<TriInfo> triInfos(simplifiedTriCount);

        for (uint32_t t = 0; t < simplifiedTriCount; t++) {
            uint32_t i0 = simplifiedIndices[t * 3 + 0];
            uint32_t i1 = simplifiedIndices[t * 3 + 1];
            uint32_t i2 = simplifiedIndices[t * 3 + 2];

            triInfos[t].triIdx = t;
            triInfos[t].centroid = (simplifiedVerts[i0].position +
                                    simplifiedVerts[i1].position +
                                    simplifiedVerts[i2].position) / 3.0f;
        }

        // Recursive k-d tree partitioning
        std::vector<std::vector<uint32_t>> clusterTriLists(targetNumClusters);

        std::function<void(std::vector<uint32_t>&, uint32_t, uint32_t)> kdPartition;
        kdPartition = [&](std::vector<uint32_t>& triIndices, uint32_t startCluster, uint32_t numClusters) {
            if (numClusters <= 1 || triIndices.empty()) {
                for (uint32_t ti : triIndices) {
                    clusterTriLists[startCluster].push_back(ti);
                }
                return;
            }

            // Find bounding box and longest axis
            glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
            for (uint32_t ti : triIndices) {
                minBounds = glm::min(minBounds, triInfos[ti].centroid);
                maxBounds = glm::max(maxBounds, triInfos[ti].centroid);
            }

            glm::vec3 extent = maxBounds - minBounds;
            int axis = 0;
            if (extent.y > extent.x && extent.y > extent.z) axis = 1;
            else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

            // Sort and split along longest axis
            std::sort(triIndices.begin(), triIndices.end(), [&](uint32_t a, uint32_t b) {
                return triInfos[a].centroid[axis] < triInfos[b].centroid[axis];
            });

            uint32_t midPoint = static_cast<uint32_t>(triIndices.size()) / 2;
            uint32_t leftClusters = numClusters / 2;
            uint32_t rightClusters = numClusters - leftClusters;

            std::vector<uint32_t> leftTris(triIndices.begin(), triIndices.begin() + midPoint);
            std::vector<uint32_t> rightTris(triIndices.begin() + midPoint, triIndices.end());

            kdPartition(leftTris, startCluster, leftClusters);
            kdPartition(rightTris, startCluster + leftClusters, rightClusters);
        };

        std::vector<uint32_t> allTriIndices(simplifiedTriCount);
        for (uint32_t t = 0; t < simplifiedTriCount; t++) {
            allTriIndices[t] = t;
        }
        kdPartition(allTriIndices, 0, targetNumClusters);

        // Create clusters from partition results
        for (uint32_t c = 0; c < targetNumClusters; c++) {
            const auto& triList = clusterTriLists[c];
            if (triList.empty()) continue;

            std::vector<ClusterVertex> clusterVerts;
            std::vector<uint32_t> clusterIndices;
            std::unordered_map<uint32_t, uint32_t> vertRemap;

            for (uint32_t triLocalIdx : triList) {
                uint32_t origTri = triInfos[triLocalIdx].triIdx;
                for (int v = 0; v < 3; v++) {
                    uint32_t srcIdx = simplifiedIndices[origTri * 3 + v];
                    auto it = vertRemap.find(srcIdx);
                    if (it == vertRemap.end()) {
                        uint32_t newIdx = static_cast<uint32_t>(clusterVerts.size());
                        clusterVerts.push_back(simplifiedVerts[srcIdx]);
                        vertRemap[srcIdx] = newIdx;
                        clusterIndices.push_back(newIdx);
                    } else {
                        clusterIndices.push_back(it->second);
                    }
                }
            }

            if (clusterIndices.empty()) continue;

            Cluster newCluster{};
            newCluster.clusterId = static_cast<uint32_t>(mesh.clusters.size());
            newCluster.lodLevel = targetLevel;
            newCluster.meshId = mesh.meshId;
            newCluster.vertexOffset = globalVertexOffset;
            newCluster.vertexCount = static_cast<uint32_t>(clusterVerts.size());
            newCluster.indexOffset = globalIndexOffset;
            newCluster.triangleCount = static_cast<uint32_t>(clusterIndices.size()) / 3;

            updateClusterBounds(newCluster, clusterVerts, 0, newCluster.vertexCount);

            float maxChildError = 0.0f;
            for (uint32_t childIdx : sourceClusters) {
                maxChildError = std::max(maxChildError, mesh.clusters[childIdx].lodError);
            }
            newCluster.lodError = maxChildError + 0.01f * targetLevel;
            newCluster.parentError = newCluster.lodError;
            newCluster.maxChildError = maxChildError;

            // For multi-cluster LOD, child linking is approximate
            uint32_t childStart = c * (static_cast<uint32_t>(sourceClusters.size()) / targetNumClusters);
            uint32_t childEnd = (c + 1) * (static_cast<uint32_t>(sourceClusters.size()) / targetNumClusters);
            childEnd = std::min(childEnd, static_cast<uint32_t>(sourceClusters.size()));

            newCluster.childClusterStart = sourceClusters[childStart];
            newCluster.childClusterCount = childEnd - childStart;

            newCluster.materialIndex = mesh.clusters[sourceClusters[0]].materialIndex;
            newCluster.flags = CLUSTER_FLAG_RESIDENT;

            // Generate debug color
            float hue = std::fmod(newCluster.clusterId * 0.618033988749895f, 1.0f);
            float s = 0.5f + 0.3f * (targetLevel / 5.0f);
            float vb = 0.9f - 0.1f * targetLevel;
            int hi = static_cast<int>(hue * 6.0f);
            float f = hue * 6.0f - hi;
            float p = vb * (1 - s);
            float q = vb * (1 - f * s);
            float t = vb * (1 - (1 - f) * s);
            glm::vec3 rgb;
            switch (hi % 6) {
                case 0: rgb = glm::vec3(vb, t, p); break;
                case 1: rgb = glm::vec3(q, vb, p); break;
                case 2: rgb = glm::vec3(p, vb, t); break;
                case 3: rgb = glm::vec3(p, q, vb); break;
                case 4: rgb = glm::vec3(t, p, vb); break;
                case 5: rgb = glm::vec3(vb, p, q); break;
            }
            newCluster.debugColor = glm::vec4(rgb, 1.0f);

            mesh.clusters.push_back(newCluster);

            for (const auto& vert : clusterVerts) {
                mesh.vertices.push_back(vert);
            }
            for (uint32_t idx : clusterIndices) {
                mesh.indices.push_back(idx);
            }

            globalVertexOffset += newCluster.vertexCount;
            globalIndexOffset += static_cast<uint32_t>(clusterIndices.size());
        }

        // Update all source clusters to point to new parents (simplified)
        uint32_t firstNewCluster = static_cast<uint32_t>(mesh.clusters.size()) - targetNumClusters;
        for (uint32_t childIdx : sourceClusters) {
            mesh.clusters[childIdx].parentClusterStart = firstNewCluster;
            mesh.clusters[childIdx].parentClusterCount = targetNumClusters;
        }
    }

    return true;
}

void ClusterDAGBuilder::simplifyClusterGroup(const std::vector<uint32_t>& sourceClusterIndices,
                                              const ClusteredMesh& mesh,
                                              std::vector<ClusterVertex>& outVertices,
                                              std::vector<uint32_t>& outIndices,
                                              float targetReduction) {
    // Combine all cluster geometry with vertex welding
    // This is critical: clusters share boundary vertices that have the same position
    // but different indices. We need to merge them for proper simplification.

    std::vector<ClusterVertex> combinedVerts;
    std::vector<uint32_t> combinedIndices;

    // Hash function for vertex position (for welding)
    auto positionHash = [](const glm::vec3& p) -> uint64_t {
        // Quantize to avoid floating point issues (1/1000 precision)
        int32_t x = static_cast<int32_t>(p.x * 1000.0f);
        int32_t y = static_cast<int32_t>(p.y * 1000.0f);
        int32_t z = static_cast<int32_t>(p.z * 1000.0f);
        return (uint64_t(x & 0xFFFFF) << 40) | (uint64_t(y & 0xFFFFF) << 20) | uint64_t(z & 0xFFFFF);
    };

    // Map from position hash to vertex index in combined mesh
    std::unordered_map<uint64_t, uint32_t> positionToIndex;

    for (uint32_t clusterIdx : sourceClusterIndices) {
        const Cluster& c = mesh.clusters[clusterIdx];

        // Build local index remap for this cluster (old local index -> new combined index)
        std::vector<uint32_t> localToGlobal(c.vertexCount);

        for (uint32_t i = 0; i < c.vertexCount; i++) {
            const ClusterVertex& v = mesh.vertices[c.vertexOffset + i];
            uint64_t hash = positionHash(v.position);

            auto it = positionToIndex.find(hash);
            if (it != positionToIndex.end()) {
                // Vertex already exists, reuse it
                localToGlobal[i] = it->second;
            } else {
                // New vertex
                uint32_t newIdx = static_cast<uint32_t>(combinedVerts.size());
                combinedVerts.push_back(v);
                positionToIndex[hash] = newIdx;
                localToGlobal[i] = newIdx;
            }
        }

        // Copy indices with remapping
        for (uint32_t i = 0; i < c.triangleCount * 3; i++) {
            uint32_t localIdx = mesh.indices[c.indexOffset + i];
            combinedIndices.push_back(localToGlobal[localIdx]);
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
    // Legacy function - now use mergeAdjacentClustersAtLevel instead
    clusterGroups.clear();
}

void ClusterDAGBuilder::mergeAdjacentClustersAtLevel(const std::vector<Cluster>& allClusters,
                                                      const std::vector<uint32_t>& clusterIndices,
                                                      std::vector<std::vector<uint32_t>>& clusterGroups) {
    // Spatial grouping based on bounding sphere overlap - only considers specified clusters
    if (clusterIndices.size() < 2) {
        clusterGroups.clear();
        return;
    }

    std::unordered_set<uint32_t> indexSet(clusterIndices.begin(), clusterIndices.end());
    std::unordered_set<uint32_t> grouped;
    clusterGroups.clear();

    for (uint32_t i : clusterIndices) {
        if (grouped.count(i)) continue;

        std::vector<uint32_t> group;
        group.push_back(i);
        grouped.insert(i);

        const Cluster& ci = allClusters[i];

        // Find adjacent clusters from the same level
        // Build list of candidates sorted by distance
        std::vector<std::pair<float, uint32_t>> candidates;
        for (uint32_t j : clusterIndices) {
            if (grouped.count(j) || j == i) continue;

            const Cluster& cj = allClusters[j];
            float distance = glm::length(ci.boundingSphereCenter - cj.boundingSphereCenter);
            candidates.push_back({distance, j});
        }

        // Sort by distance
        std::sort(candidates.begin(), candidates.end());

        // Add nearest neighbors that are close enough
        for (const auto& candidate : candidates) {
            if (grouped.count(candidate.second)) continue;

            const Cluster& cj = allClusters[candidate.second];
            float threshold = (ci.boundingSphereRadius + cj.boundingSphereRadius) * 2.0f;

            if (candidate.first < threshold) {
                group.push_back(candidate.second);
                grouped.insert(candidate.second);

                // Limit group size to 4 clusters (produces ~2 clusters at next level)
                if (group.size() >= 4) break;
            }
        }

        if (group.size() >= 2) {
            clusterGroups.push_back(group);
        }
    }

    // Handle ungrouped clusters - try to pair them with nearest ungrouped neighbor
    std::vector<uint32_t> ungrouped;
    for (uint32_t i : clusterIndices) {
        if (!grouped.count(i)) {
            ungrouped.push_back(i);
        }
    }

    // Pair remaining ungrouped clusters
    while (ungrouped.size() >= 2) {
        uint32_t first = ungrouped[0];
        float minDist = FLT_MAX;
        size_t nearestIdx = 1;

        for (size_t j = 1; j < ungrouped.size(); j++) {
            float dist = glm::length(allClusters[first].boundingSphereCenter -
                                     allClusters[ungrouped[j]].boundingSphereCenter);
            if (dist < minDist) {
                minDist = dist;
                nearestIdx = j;
            }
        }

        clusterGroups.push_back({first, ungrouped[nearestIdx]});
        ungrouped.erase(ungrouped.begin() + nearestIdx);
        ungrouped.erase(ungrouped.begin());
    }

    // If one cluster remains, add it to the nearest existing group
    if (ungrouped.size() == 1 && !clusterGroups.empty()) {
        uint32_t remaining = ungrouped[0];
        float minDist = FLT_MAX;
        size_t nearestGroup = 0;

        for (size_t g = 0; g < clusterGroups.size(); g++) {
            for (uint32_t idx : clusterGroups[g]) {
                float dist = glm::length(allClusters[remaining].boundingSphereCenter -
                                         allClusters[idx].boundingSphereCenter);
                if (dist < minDist) {
                    minDist = dist;
                    nearestGroup = g;
                }
            }
        }
        clusterGroups[nearestGroup].push_back(remaining);
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
    // Use vertex clustering instead of edge collapse for better surface preservation
    // This approach groups nearby vertices and replaces them with a representative vertex

    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;

    if (numTriangles <= targetTriangles) {
        return;  // Already at or below target
    }

    // Calculate mesh bounds for grid sizing
    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);
    for (const auto& v : vertices) {
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
    }

    glm::vec3 extent = maxBounds - minBounds;
    float maxExtent = std::max({extent.x, extent.y, extent.z});

    // Calculate grid resolution based on target triangle count
    // For manifold meshes: triangles ≈ 2 * vertices (Euler characteristic)
    // With vertex clustering: output_tris ≈ 2 * gridRes^3 (for 3D grid)
    // But in practice it's closer to gridRes^2 for surface meshes

    // Target: achieve roughly targetTriangles output
    // gridRes ≈ sqrt(targetTriangles / 2)
    int gridRes = static_cast<int>(std::sqrt(static_cast<float>(targetTriangles) * 0.5f));
    gridRes = std::max(3, std::min(gridRes, 256));  // Allow higher resolution for large meshes

    float cellSize = maxExtent / gridRes;
    if (cellSize < 0.00001f) cellSize = 0.00001f;

    // Debug output
    // std::cout << "Simplify: " << numTriangles << " -> " << targetTriangles
    //           << " tris, gridRes=" << gridRes << ", cellSize=" << cellSize << std::endl;

    // Hash function for grid cell
    auto gridHash = [&](const glm::vec3& p) -> uint64_t {
        int x = static_cast<int>((p.x - minBounds.x) / cellSize);
        int y = static_cast<int>((p.y - minBounds.y) / cellSize);
        int z = static_cast<int>((p.z - minBounds.z) / cellSize);
        x = std::max(0, std::min(x, gridRes - 1));
        y = std::max(0, std::min(y, gridRes - 1));
        z = std::max(0, std::min(z, gridRes - 1));
        return (uint64_t(x) << 40) | (uint64_t(y) << 20) | uint64_t(z);
    };

    // Group vertices by grid cell
    std::unordered_map<uint64_t, std::vector<uint32_t>> cellVertices;
    for (uint32_t i = 0; i < vertices.size(); i++) {
        uint64_t hash = gridHash(vertices[i].position);
        cellVertices[hash].push_back(i);
    }

    // Create representative vertex for each cell (average position/normal)
    std::vector<ClusterVertex> newVertices;
    std::unordered_map<uint64_t, uint32_t> cellToNewVertex;
    std::vector<uint32_t> oldToNew(vertices.size());

    for (auto& cell : cellVertices) {
        uint64_t hash = cell.first;
        const auto& vertList = cell.second;

        // Compute average position and normal for this cell
        glm::vec3 avgPos(0.0f);
        glm::vec3 avgNormal(0.0f);
        glm::vec2 avgTexCoord(0.0f);

        for (uint32_t vi : vertList) {
            avgPos += vertices[vi].position;
            avgNormal += vertices[vi].normal;
            avgTexCoord += vertices[vi].texCoord;
        }

        float count = static_cast<float>(vertList.size());
        avgPos /= count;
        avgNormal = glm::normalize(avgNormal);
        avgTexCoord /= count;

        // Create representative vertex
        uint32_t newIdx = static_cast<uint32_t>(newVertices.size());
        ClusterVertex repVert;
        repVert.position = avgPos;
        repVert.normal = avgNormal;
        repVert.texCoord = avgTexCoord;
        newVertices.push_back(repVert);

        cellToNewVertex[hash] = newIdx;

        // Map all old vertices in this cell to the new representative
        for (uint32_t vi : vertList) {
            oldToNew[vi] = newIdx;
        }
    }

    // Rebuild indices using new vertex mapping
    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());

    for (uint32_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = oldToNew[indices[t * 3 + 0]];
        uint32_t i1 = oldToNew[indices[t * 3 + 1]];
        uint32_t i2 = oldToNew[indices[t * 3 + 2]];

        // Skip degenerate triangles (where 2+ vertices collapsed to same point)
        if (i0 != i1 && i1 != i2 && i2 != i0) {
            newIndices.push_back(i0);
            newIndices.push_back(i1);
            newIndices.push_back(i2);
        }
    }

    // Remove duplicate triangles
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> uniqueTris;
    std::vector<uint32_t> finalIndices;
    finalIndices.reserve(newIndices.size());

    for (size_t t = 0; t < newIndices.size() / 3; t++) {
        uint32_t a = newIndices[t * 3 + 0];
        uint32_t b = newIndices[t * 3 + 1];
        uint32_t c = newIndices[t * 3 + 2];

        // Normalize triangle (smallest index first)
        if (a > b) std::swap(a, b);
        if (a > c) std::swap(a, c);
        if (b > c) std::swap(b, c);

        auto tri = std::make_tuple(a, b, c);
        if (uniqueTris.find(tri) == uniqueTris.end()) {
            uniqueTris.insert(tri);
            finalIndices.push_back(newIndices[t * 3 + 0]);
            finalIndices.push_back(newIndices[t * 3 + 1]);
            finalIndices.push_back(newIndices[t * 3 + 2]);
        }
    }

    // Compact vertices (remove unreferenced)
    std::unordered_set<uint32_t> usedVerts(finalIndices.begin(), finalIndices.end());
    std::vector<ClusterVertex> compactVerts;
    std::unordered_map<uint32_t, uint32_t> finalRemap;

    for (uint32_t v : usedVerts) {
        finalRemap[v] = static_cast<uint32_t>(compactVerts.size());
        compactVerts.push_back(newVertices[v]);
    }

    for (uint32_t& idx : finalIndices) {
        idx = finalRemap[idx];
    }

    vertices = std::move(compactVerts);
    indices = std::move(finalIndices);
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
    // For curved surfaces like spheres, prefer endpoints over midpoint
    // to avoid "shrinking" the mesh inward.
    //
    // The midpoint of two surface vertices on a sphere lies INSIDE the sphere,
    // causing the "punched in" appearance.

    // Evaluate costs at endpoints only
    float cost0 = q.evaluate(v0);
    float cost1 = q.evaluate(v1);

    // Prefer endpoint with lower error
    // This preserves surface curvature better than midpoint
    if (cost0 <= cost1) {
        return v0;
    } else {
        return v1;
    }
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
