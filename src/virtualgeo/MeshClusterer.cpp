#include "include/virtualgeo/MeshClusterer.h"
#include "include/virtualgeo/GraphPartitioner.h"
#include "include/mesh/Mesh.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <queue>
#include <cmath>

namespace MiEngine {

// ============================================================================
// ClusteringStats
// ============================================================================

void ClusteringStats::print() const {
    std::cout << "=== Clustering Statistics ===" << std::endl;
    std::cout << "Input:  " << inputTriangles << " triangles, " << inputVertices << " vertices" << std::endl;
    std::cout << "Output: " << outputClusters << " clusters, " << lodLevels << " LOD levels" << std::endl;
    std::cout << "Average cluster size: " << averageClusterSize << " triangles" << std::endl;
    std::cout << "Clustering time: " << clusteringTime << " ms" << std::endl;
    std::cout << "DAG build time: " << dagBuildTime << " ms" << std::endl;
    std::cout << "Total time: " << totalTime << " ms" << std::endl;
}

// ============================================================================
// TriangleAdjacency
// ============================================================================

void TriangleAdjacency::build(const std::vector<uint32_t>& indices, uint32_t vertexCount) {
    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
    neighbors.resize(numTriangles);

    // Build edge-to-triangle map
    // Edge key = min(v0,v1) * vertexCount + max(v0,v1)
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTriangles;

    auto makeEdgeKey = [vertexCount](uint32_t v0, uint32_t v1) -> uint64_t {
        if (v0 > v1) std::swap(v0, v1);
        return static_cast<uint64_t>(v0) * vertexCount + v1;
    };

    for (uint32_t tri = 0; tri < numTriangles; tri++) {
        uint32_t i0 = indices[tri * 3 + 0];
        uint32_t i1 = indices[tri * 3 + 1];
        uint32_t i2 = indices[tri * 3 + 2];

        edgeToTriangles[makeEdgeKey(i0, i1)].push_back(tri);
        edgeToTriangles[makeEdgeKey(i1, i2)].push_back(tri);
        edgeToTriangles[makeEdgeKey(i2, i0)].push_back(tri);
    }

    // Build adjacency from shared edges
    for (const auto& [edge, triangles] : edgeToTriangles) {
        for (size_t i = 0; i < triangles.size(); i++) {
            for (size_t j = i + 1; j < triangles.size(); j++) {
                uint32_t t0 = triangles[i];
                uint32_t t1 = triangles[j];
                neighbors[t0].push_back(t1);
                neighbors[t1].push_back(t0);
            }
        }
    }

    // Remove duplicates in neighbor lists
    for (auto& neighborList : neighbors) {
        std::sort(neighborList.begin(), neighborList.end());
        neighborList.erase(std::unique(neighborList.begin(), neighborList.end()), neighborList.end());
    }
}

void TriangleAdjacency::buildFromPositions(const std::vector<Vertex>& vertices,
                                            const std::vector<uint32_t>& indices,
                                            float positionTolerance) {
    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
    neighbors.resize(numTriangles);

    // Use spatial hashing to find vertices at the same position
    // Hash key is quantized position
    float invTolerance = 1.0f / positionTolerance;

    auto hashPosition = [invTolerance](const glm::vec3& pos) -> uint64_t {
        int32_t x = static_cast<int32_t>(std::floor(pos.x * invTolerance));
        int32_t y = static_cast<int32_t>(std::floor(pos.y * invTolerance));
        int32_t z = static_cast<int32_t>(std::floor(pos.z * invTolerance));
        // Combine into single hash
        uint64_t hx = static_cast<uint64_t>(x + 1000000) & 0xFFFFF;  // 20 bits
        uint64_t hy = static_cast<uint64_t>(y + 1000000) & 0xFFFFF;  // 20 bits
        uint64_t hz = static_cast<uint64_t>(z + 1000000) & 0xFFFFF;  // 20 bits
        return (hx << 40) | (hy << 20) | hz;
    };

    // Map from position hash to canonical vertex index
    std::unordered_map<uint64_t, uint32_t> positionToCanonical;
    std::vector<uint32_t> vertexToCanonical(vertices.size());

    for (uint32_t i = 0; i < vertices.size(); i++) {
        uint64_t hash = hashPosition(vertices[i].position);
        auto it = positionToCanonical.find(hash);
        if (it != positionToCanonical.end()) {
            vertexToCanonical[i] = it->second;
        } else {
            positionToCanonical[hash] = i;
            vertexToCanonical[i] = i;
        }
    }

    // Now build edge-to-triangle map using canonical vertex indices
    // Edge key = sorted pair of canonical vertex indices
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTriangles;
    uint32_t numCanonical = static_cast<uint32_t>(positionToCanonical.size());

    auto makeEdgeKey = [numCanonical](uint32_t v0, uint32_t v1) -> uint64_t {
        if (v0 > v1) std::swap(v0, v1);
        return static_cast<uint64_t>(v0) * (numCanonical + 1) + v1;
    };

    for (uint32_t tri = 0; tri < numTriangles; tri++) {
        uint32_t i0 = vertexToCanonical[indices[tri * 3 + 0]];
        uint32_t i1 = vertexToCanonical[indices[tri * 3 + 1]];
        uint32_t i2 = vertexToCanonical[indices[tri * 3 + 2]];

        // Skip degenerate triangles
        if (i0 == i1 || i1 == i2 || i2 == i0) continue;

        edgeToTriangles[makeEdgeKey(i0, i1)].push_back(tri);
        edgeToTriangles[makeEdgeKey(i1, i2)].push_back(tri);
        edgeToTriangles[makeEdgeKey(i2, i0)].push_back(tri);
    }

    // Build adjacency from shared edges
    for (const auto& [edge, triangles] : edgeToTriangles) {
        for (size_t i = 0; i < triangles.size(); i++) {
            for (size_t j = i + 1; j < triangles.size(); j++) {
                uint32_t t0 = triangles[i];
                uint32_t t1 = triangles[j];
                neighbors[t0].push_back(t1);
                neighbors[t1].push_back(t0);
            }
        }
    }

    // Remove duplicates in neighbor lists
    for (auto& neighborList : neighbors) {
        std::sort(neighborList.begin(), neighborList.end());
        neighborList.erase(std::unique(neighborList.begin(), neighborList.end()), neighborList.end());
    }

    // Debug: count total edges
    uint32_t totalEdges = 0;
    for (const auto& nl : neighbors) {
        totalEdges += static_cast<uint32_t>(nl.size());
    }
    std::cout << "  Position-based adjacency: " << numCanonical << " unique positions, "
              << totalEdges / 2 << " edges" << std::endl;
}

// ============================================================================
// MeshClusterer Implementation
// ============================================================================

MeshClusterer::MeshClusterer() {}

MeshClusterer::~MeshClusterer() {}

bool MeshClusterer::isMetisAvailable() {
    // We now use our built-in GraphPartitioner (METIS-like algorithm)
    // No external dependency needed
    return true;
}

bool MeshClusterer::clusterMesh(const std::vector<Vertex>& vertices,
                                 const std::vector<uint32_t>& indices,
                                 const ClusteringOptions& options,
                                 ClusteredMesh& outMesh) {
    auto startTime = std::chrono::high_resolution_clock::now();

    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
    uint32_t numVertices = static_cast<uint32_t>(vertices.size());

    if (numTriangles == 0) {
        std::cerr << "MeshClusterer: No triangles to cluster" << std::endl;
        return false;
    }

    m_Stats = ClusteringStats{};
    m_Stats.inputTriangles = numTriangles;
    m_Stats.inputVertices = numVertices;

    if (options.verbose) {
        std::cout << "MeshClusterer: Clustering " << numTriangles << " triangles..." << std::endl;
    }

    // Step 1: Build triangle adjacency graph
    std::cout << "  Step 1: Building adjacency graph..." << std::endl;
    TriangleAdjacency adjacency;
    buildAdjacencyGraph(indices, numVertices, adjacency);

    // Check if adjacency graph has edges - if not, mesh likely has duplicate vertices
    // (e.g., 3 unique vertices per triangle with no sharing)
    uint32_t totalEdges = 0;
    for (const auto& neighbors : adjacency.neighbors) {
        totalEdges += static_cast<uint32_t>(neighbors.size());
    }
    totalEdges /= 2;  // Each edge counted twice

    if (totalEdges == 0 && numTriangles > 1) {
        std::cout << "  Index-based adjacency found no edges (mesh has duplicate vertices)" << std::endl;
        std::cout << "  Rebuilding adjacency using vertex positions..." << std::endl;
        adjacency.buildFromPositions(vertices, indices, 0.0001f);
    } else {
        std::cout << "  Adjacency built: " << adjacency.neighbors.size() << " triangles, "
                  << totalEdges << " edges" << std::endl;
    }

    // Step 2: Partition triangles into clusters
    uint32_t targetClusterCount = (numTriangles + options.targetClusterSize - 1) / options.targetClusterSize;
    targetClusterCount = std::max(targetClusterCount, 1u);
    std::cout << "  Step 2: Partitioning into " << targetClusterCount << " clusters..." << std::endl;

    // Compute triangle centroids for spatial partitioning
    std::vector<glm::vec3> triangleCentroids(numTriangles);
    for (uint32_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];
        triangleCentroids[t] = (vertices[i0].position + vertices[i1].position + vertices[i2].position) / 3.0f;
    }

    std::vector<uint32_t> clusterAssignment(numTriangles);

    auto partitionStart = std::chrono::high_resolution_clock::now();

    // Use graph partitioner for cluster coherence
    GraphPartitioner partitioner;
    GraphPartitionerOptions partOpts;
    partOpts.targetPartitions = targetClusterCount;
    partOpts.minPartitionSize = options.minClusterSize;
    partOpts.verbose = options.verbose;

#if USE_METIS_LIBRARY
    // Use METIS library with spatial post-processing (best quality)
    if (!partitioner.partitionMETISSpatial(adjacency.neighbors, triangleCentroids, numTriangles, partOpts, clusterAssignment)) {
        if (options.verbose) {
            std::cout << "MeshClusterer: METIS failed, trying custom spatial partitioner" << std::endl;
        }
#endif
        // Fallback: Use custom spatial partitioning for compact clusters
        if (!partitioner.partitionSpatial(adjacency.neighbors, triangleCentroids, numTriangles, partOpts, clusterAssignment)) {
            if (options.verbose) {
                std::cout << "MeshClusterer: Spatial partitioner failed, trying regular partitioner" << std::endl;
            }
            // Fallback to regular partitioner
            if (!partitioner.partition(adjacency.neighbors, numTriangles, partOpts, clusterAssignment)) {
                if (options.verbose) {
                    std::cout << "MeshClusterer: Partitioner failed, falling back to greedy" << std::endl;
                }
                partitionGreedy(adjacency, numTriangles, options.targetClusterSize, clusterAssignment);
            }
        }
#if USE_METIS_LIBRARY
    }
#endif

    auto partitionEnd = std::chrono::high_resolution_clock::now();
    m_Stats.clusteringTime = std::chrono::duration<float, std::milli>(partitionEnd - partitionStart).count();

    // Count actual number of clusters
    uint32_t maxClusterId = 0;
    for (uint32_t assignment : clusterAssignment) {
        maxClusterId = std::max(maxClusterId, assignment);
    }
    uint32_t numClusters = maxClusterId + 1;

    // Step 3: Create cluster objects
    createClustersFromPartition(vertices, indices, clusterAssignment, numClusters, options, outMesh);

    // Step 4: Compute mesh-wide bounds
    computeMeshBounds(outMesh);

    // Finalize stats
    auto endTime = std::chrono::high_resolution_clock::now();
    m_Stats.outputClusters = static_cast<uint32_t>(outMesh.clusters.size());
    m_Stats.lodLevels = 1;  // LOD 0 only at this stage
    m_Stats.averageClusterSize = static_cast<float>(numTriangles) / m_Stats.outputClusters;
    m_Stats.totalTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    if (options.verbose) {
        m_Stats.print();
    }

    return true;
}

bool MeshClusterer::clusterMesh(const ::Mesh& mesh,
                                 const ClusteringOptions& options,
                                 ClusteredMesh& outMesh) {
    // Get raw data from mesh
    const auto& meshVertices = mesh.getVertexData();
    const auto& meshIndices = mesh.getIndexData();

    // Convert indices to uint32_t
    std::vector<uint32_t> indices;
    indices.reserve(meshIndices.size());
    for (unsigned int idx : meshIndices) {
        indices.push_back(static_cast<uint32_t>(idx));
    }

    outMesh.name = mesh.getName();

    return clusterMesh(meshVertices, indices, options, outMesh);
}

void MeshClusterer::buildAdjacencyGraph(const std::vector<uint32_t>& indices,
                                         uint32_t vertexCount,
                                         TriangleAdjacency& adjacency) {
    adjacency.build(indices, vertexCount);
}

void MeshClusterer::partitionGreedy(const TriangleAdjacency& adjacency,
                                     uint32_t numTriangles,
                                     uint32_t targetClusterSize,
                                     std::vector<uint32_t>& clusterAssignment) {
    // Greedy BFS-based clustering
    // Start from unassigned triangles, grow clusters by adding neighbors

    std::fill(clusterAssignment.begin(), clusterAssignment.end(), UINT32_MAX);

    uint32_t currentCluster = 0;
    uint32_t assigned = 0;

    while (assigned < numTriangles) {
        // Find first unassigned triangle
        uint32_t seed = UINT32_MAX;
        for (uint32_t i = 0; i < numTriangles; i++) {
            if (clusterAssignment[i] == UINT32_MAX) {
                seed = i;
                break;
            }
        }

        if (seed == UINT32_MAX) break;

        // BFS to grow cluster
        std::queue<uint32_t> queue;
        queue.push(seed);
        clusterAssignment[seed] = currentCluster;
        assigned++;

        uint32_t clusterSize = 1;

        while (!queue.empty() && clusterSize < targetClusterSize) {
            uint32_t current = queue.front();
            queue.pop();

            for (uint32_t neighbor : adjacency.neighbors[current]) {
                if (clusterAssignment[neighbor] == UINT32_MAX && clusterSize < targetClusterSize) {
                    clusterAssignment[neighbor] = currentCluster;
                    queue.push(neighbor);
                    clusterSize++;
                    assigned++;
                }
            }
        }

        currentCluster++;
    }
}

void MeshClusterer::createClustersFromPartition(const std::vector<Vertex>& vertices,
                                                 const std::vector<uint32_t>& indices,
                                                 const std::vector<uint32_t>& clusterAssignment,
                                                 uint32_t numClusters,
                                                 const ClusteringOptions& options,
                                                 ClusteredMesh& outMesh) {
    uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;

    // Group triangles by cluster
    std::vector<std::vector<uint32_t>> clusterTriangles(numClusters);
    for (uint32_t tri = 0; tri < numTriangles; tri++) {
        uint32_t cluster = clusterAssignment[tri];
        clusterTriangles[cluster].push_back(tri);
    }

    // Create clusters
    outMesh.clusters.clear();
    outMesh.vertices.clear();
    outMesh.indices.clear();
    outMesh.meshId = 0;
    outMesh.maxLodLevel = 0;
    outMesh.rootClusterStart = 0;
    outMesh.rootClusterCount = 0;
    outMesh.leafClusterStart = 0;
    outMesh.totalTriangles = numTriangles;
    outMesh.totalVertices = 0;

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;

    for (uint32_t c = 0; c < numClusters; c++) {
        const auto& triangles = clusterTriangles[c];
        if (triangles.empty()) continue;

        // Remap vertices for this cluster
        std::vector<ClusterVertex> clusterVerts;
        std::vector<uint32_t> clusterIndices;
        remapClusterVertices(vertices, indices, triangles, clusterVerts, clusterIndices);

        if (clusterVerts.empty()) continue;

        // Create cluster
        Cluster cluster{};
        cluster.clusterId = static_cast<uint32_t>(outMesh.clusters.size());
        cluster.lodLevel = 0;  // Finest detail
        cluster.meshId = outMesh.meshId;

        cluster.vertexOffset = globalVertexOffset;
        cluster.vertexCount = static_cast<uint32_t>(clusterVerts.size());
        cluster.indexOffset = globalIndexOffset;
        cluster.triangleCount = static_cast<uint32_t>(clusterIndices.size()) / 3;

        // Compute bounds
        computeClusterBounds(clusterVerts, 0, cluster.vertexCount, cluster);

        // LOD error for leaf clusters is 0
        cluster.lodError = 0.0f;
        cluster.parentError = 0.0f;
        cluster.screenSpaceError = 0.0f;
        cluster.maxChildError = 0.0f;

        // No parents or children yet (DAG not built)
        cluster.parentClusterStart = 0;
        cluster.parentClusterCount = 0;
        cluster.childClusterStart = 0;
        cluster.childClusterCount = 0;

        cluster.materialIndex = 0;
        cluster.flags = CLUSTER_FLAG_RESIDENT;

        if (options.generateDebugColors) {
            cluster.debugColor = generateDebugColor(cluster.clusterId);
        } else {
            cluster.debugColor = glm::vec4(1.0f);
        }

        // Append to mesh data
        outMesh.clusters.push_back(cluster);

        for (const auto& v : clusterVerts) {
            outMesh.vertices.push_back(v);
        }
        for (uint32_t idx : clusterIndices) {
            outMesh.indices.push_back(idx);
        }

        globalVertexOffset += cluster.vertexCount;
        globalIndexOffset += static_cast<uint32_t>(clusterIndices.size());
    }

    outMesh.leafClusterCount = static_cast<uint32_t>(outMesh.clusters.size());
    outMesh.totalVertices = static_cast<uint32_t>(outMesh.vertices.size());
}

void MeshClusterer::computeClusterBounds(const std::vector<ClusterVertex>& vertices,
                                          uint32_t vertexOffset,
                                          uint32_t vertexCount,
                                          Cluster& cluster) {
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

    // Compute bounding sphere radius
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

void MeshClusterer::computeMeshBounds(ClusteredMesh& mesh) {
    if (mesh.clusters.empty()) return;

    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);
    glm::vec3 centroid(0.0f);
    float totalWeight = 0.0f;

    for (const auto& cluster : mesh.clusters) {
        minBounds = glm::min(minBounds, cluster.aabbMin);
        maxBounds = glm::max(maxBounds, cluster.aabbMax);

        float weight = static_cast<float>(cluster.triangleCount);
        centroid += cluster.boundingSphereCenter * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0f) {
        centroid /= totalWeight;
    }

    // Compute bounding sphere that encompasses all clusters
    float maxRadius = 0.0f;
    for (const auto& cluster : mesh.clusters) {
        float dist = glm::length(cluster.boundingSphereCenter - centroid) + cluster.boundingSphereRadius;
        maxRadius = std::max(maxRadius, dist);
    }

    mesh.boundingSphereCenter = centroid;
    mesh.boundingSphereRadius = maxRadius;
    mesh.aabbMin = minBounds;
    mesh.aabbMax = maxBounds;
}

glm::vec4 MeshClusterer::generateDebugColor(uint32_t clusterId) {
    // Generate distinct colors using golden ratio
    const float goldenRatio = 0.618033988749895f;
    float hue = std::fmod(clusterId * goldenRatio, 1.0f);

    // HSV to RGB (saturation = 0.7, value = 0.9)
    float s = 0.7f;
    float v = 0.9f;

    float h = hue * 6.0f;
    int i = static_cast<int>(h);
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    glm::vec3 rgb;
    switch (i % 6) {
        case 0: rgb = glm::vec3(v, t, p); break;
        case 1: rgb = glm::vec3(q, v, p); break;
        case 2: rgb = glm::vec3(p, v, t); break;
        case 3: rgb = glm::vec3(p, q, v); break;
        case 4: rgb = glm::vec3(t, p, v); break;
        case 5: rgb = glm::vec3(v, p, q); break;
    }

    return glm::vec4(rgb, 1.0f);
}

void MeshClusterer::remapClusterVertices(const std::vector<Vertex>& srcVertices,
                                          const std::vector<uint32_t>& srcIndices,
                                          const std::vector<uint32_t>& triangleList,
                                          std::vector<ClusterVertex>& outVertices,
                                          std::vector<uint32_t>& outIndices) {
    // Map from original vertex index to cluster-local index
    std::unordered_map<uint32_t, uint32_t> vertexRemap;

    outVertices.clear();
    outIndices.clear();
    outIndices.reserve(triangleList.size() * 3);

    for (uint32_t tri : triangleList) {
        for (int v = 0; v < 3; v++) {
            uint32_t originalIdx = srcIndices[tri * 3 + v];

            auto it = vertexRemap.find(originalIdx);
            if (it != vertexRemap.end()) {
                outIndices.push_back(it->second);
            } else {
                uint32_t newIdx = static_cast<uint32_t>(outVertices.size());
                vertexRemap[originalIdx] = newIdx;

                // Convert to ClusterVertex
                const auto& sv = srcVertices[originalIdx];
                ClusterVertex cv{};
                cv.position = sv.position;
                cv.normal = sv.normal;
                cv.texCoord = sv.texCoord;

                outVertices.push_back(cv);
                outIndices.push_back(newIdx);
            }
        }
    }
}

} // namespace MiEngine
