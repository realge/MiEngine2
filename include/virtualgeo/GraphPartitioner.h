#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Enable METIS library support (requires metis.dll and metis.h)
#define USE_METIS_LIBRARY 1

#if USE_METIS_LIBRARY
#include <metis.h>
#endif

namespace MiEngine {

// ============================================================================
// Graph Partitioner - METIS-like multilevel k-way partitioning
//
// Implements the core graph partitioning algorithm:
// 1. Coarsening phase: Contract graph by merging adjacent vertices
// 2. Initial partitioning: Partition the coarsest graph
// 3. Uncoarsening + Refinement: Project back while improving partition quality
//
// Based on the multilevel paradigm from:
// - Karypis & Kumar, "A Fast and High Quality Multilevel Scheme for
//   Partitioning Irregular Graphs" (1998)
// ============================================================================

struct GraphPartitionerOptions {
    uint32_t targetPartitions = 16;     // Number of partitions to create
    uint32_t minPartitionSize = 64;     // Minimum triangles per partition
    uint32_t maxCoarsenLevel = 20;      // Maximum coarsening levels
    float coarsenRatio = 0.5f;          // Target size reduction per level
    uint32_t refinementPasses = 10;     // FM refinement iterations per level
    bool verbose = false;
};

class GraphPartitioner {
public:
    GraphPartitioner();
    ~GraphPartitioner();

    // Main partitioning function
    // adjacency: adjacency[i] = list of neighbors of vertex i
    // numVertices: number of vertices in graph
    // options: partitioning options
    // outPartition: output partition assignment for each vertex
    // Returns: true on success
    bool partition(const std::vector<std::vector<uint32_t>>& adjacency,
                   uint32_t numVertices,
                   const GraphPartitionerOptions& options,
                   std::vector<uint32_t>& outPartition);

    // Spatial-aware partitioning (preferred for mesh clustering)
    // Uses triangle centroids to select spatially distributed seeds
    // This produces more compact, spatially coherent clusters
    bool partitionSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                          const std::vector<glm::vec3>& positions,
                          uint32_t numVertices,
                          const GraphPartitionerOptions& options,
                          std::vector<uint32_t>& outPartition);

#if USE_METIS_LIBRARY
    // Use the actual METIS library for partitioning (highest quality)
    // This is the gold standard for graph partitioning
    bool partitionMETIS(const std::vector<std::vector<uint32_t>>& adjacency,
                        uint32_t numVertices,
                        const GraphPartitionerOptions& options,
                        std::vector<uint32_t>& outPartition);

    // METIS with spatial post-processing to fix elongated clusters
    bool partitionMETISSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                               const std::vector<glm::vec3>& positions,
                               uint32_t numVertices,
                               const GraphPartitionerOptions& options,
                               std::vector<uint32_t>& outPartition);
#endif

private:
    // Coarsening: merge vertices to create smaller graph
    struct CoarseLevel {
        std::vector<std::vector<uint32_t>> adjacency;
        std::vector<uint32_t> vertexWeights;      // Number of fine vertices this represents
        std::vector<uint32_t> mapping;            // Fine vertex -> coarse vertex
        uint32_t numVertices;
    };

    // Heavy Edge Matching for coarsening
    void coarsenGraph(const std::vector<std::vector<uint32_t>>& fineAdj,
                      const std::vector<uint32_t>& fineWeights,
                      uint32_t fineVertices,
                      std::vector<std::vector<uint32_t>>& coarseAdj,
                      std::vector<uint32_t>& coarseWeights,
                      std::vector<uint32_t>& mapping,
                      uint32_t& coarseVertices);

    // Initial partitioning of coarsest graph using greedy growing
    void initialPartition(const std::vector<std::vector<uint32_t>>& adjacency,
                          const std::vector<uint32_t>& weights,
                          uint32_t numVertices,
                          uint32_t numPartitions,
                          std::vector<uint32_t>& partition);

    // Spatial-aware initial partitioning using k-means++ seed selection
    void initialPartitionSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                                  const std::vector<uint32_t>& weights,
                                  const std::vector<glm::vec3>& positions,
                                  uint32_t numVertices,
                                  uint32_t numPartitions,
                                  std::vector<uint32_t>& partition);

    // Select k spatially distributed seeds using k-means++ algorithm
    std::vector<uint32_t> selectSpatialSeeds(const std::vector<glm::vec3>& positions,
                                              uint32_t numVertices,
                                              uint32_t k);

    // Project partition from coarse to fine graph
    void projectPartition(const std::vector<uint32_t>& coarsePartition,
                          const std::vector<uint32_t>& mapping,
                          uint32_t fineVertices,
                          std::vector<uint32_t>& finePartition);

    // Fiduccia-Mattheyses refinement to improve partition quality
    void refinePartition(const std::vector<std::vector<uint32_t>>& adjacency,
                         const std::vector<uint32_t>& weights,
                         uint32_t numVertices,
                         uint32_t numPartitions,
                         std::vector<uint32_t>& partition,
                         uint32_t maxPasses);

    // Compute edge cut (number of edges crossing partition boundaries)
    uint32_t computeEdgeCut(const std::vector<std::vector<uint32_t>>& adjacency,
                            const std::vector<uint32_t>& partition) const;

    // Compute partition sizes
    void computePartitionSizes(const std::vector<uint32_t>& partition,
                               const std::vector<uint32_t>& weights,
                               uint32_t numPartitions,
                               std::vector<uint32_t>& sizes) const;

    // Balance partitions to be roughly equal size
    void balancePartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                           const std::vector<uint32_t>& weights,
                           uint32_t numVertices,
                           uint32_t numPartitions,
                           std::vector<uint32_t>& partition);

    // Merge small partitions into neighbors
    void mergeSmallPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                              std::vector<uint32_t>& partition,
                              uint32_t numVertices,
                              uint32_t minSize);

    // Ensure each partition is a single connected component
    // Splits disconnected partitions and merges small fragments into neighbors
    void ensureConnectedPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                    std::vector<uint32_t>& partition,
                                    uint32_t numVertices);

    // Post-process to fix elongated partitions
    // Detects partitions with high aspect ratio and splits them along the longest axis
    void fixElongatedPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                 const std::vector<glm::vec3>& positions,
                                 std::vector<uint32_t>& partition,
                                 uint32_t numVertices,
                                 float maxAspectRatio = 3.0f);

    std::vector<CoarseLevel> m_CoarseLevels;
};

} // namespace MiEngine
