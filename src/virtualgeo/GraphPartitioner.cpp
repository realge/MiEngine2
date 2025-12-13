#include "include/virtualgeo/GraphPartitioner.h"
#include <algorithm>
#include <queue>
#include <random>
#include <iostream>
#include <numeric>
#include <unordered_set>
#include <unordered_map>

namespace MiEngine {

// ============================================================================
// METIS Library Implementation
// ============================================================================

#if USE_METIS_LIBRARY

// Windows SEH wrapper for METIS calls (METIS uses signal handlers that can crash on Windows)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static bool s_metisCallSucceeded = false;
static int s_metisResult = METIS_ERROR;

// Wrapper to call METIS with SEH protection
static bool callMETISSafe(idx_t* nvtxs, idx_t* ncon, idx_t* xadj, idx_t* adjncy,
                           idx_t* nparts, idx_t* options, idx_t* edgecut, idx_t* part) {
    __try {
        s_metisResult = METIS_PartGraphKway(
            nvtxs, ncon, xadj, adjncy,
            nullptr, nullptr, nullptr,  // vwgt, vsize, adjwgt
            nparts,
            nullptr, nullptr,  // tpwgts, ubvec
            options,
            edgecut,
            part
        );
        s_metisCallSucceeded = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s_metisCallSucceeded = false;
        std::cerr << "METIS crashed with exception code: " << GetExceptionCode() << std::endl;
        return false;
    }
}
#endif

bool GraphPartitioner::partitionMETIS(const std::vector<std::vector<uint32_t>>& adjacency,
                                       uint32_t numVertices,
                                       const GraphPartitionerOptions& options,
                                       std::vector<uint32_t>& outPartition) {
    if (numVertices == 0) {
        return false;
    }

    if (options.targetPartitions <= 1) {
        outPartition.assign(numVertices, 0);
        return true;
    }

    if (options.targetPartitions >= numVertices) {
        outPartition.resize(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) {
            outPartition[i] = i;
        }
        return true;
    }

    // METIS requires at least 2 partitions and some edges
    if (options.targetPartitions < 2) {
        return false;
    }

    if (options.verbose) {
        std::cout << "METIS: Partitioning " << numVertices
                  << " vertices into " << options.targetPartitions << " parts" << std::endl;
    }

    // Convert adjacency list to METIS CSR format
    // xadj: index into adjncy for each vertex (size: numVertices + 1)
    // adjncy: concatenated adjacency lists
    std::vector<idx_t> xadj(numVertices + 1);
    std::vector<idx_t> adjncy;

    xadj[0] = 0;
    for (uint32_t v = 0; v < numVertices; v++) {
        for (uint32_t neighbor : adjacency[v]) {
            adjncy.push_back(static_cast<idx_t>(neighbor));
        }
        xadj[v + 1] = static_cast<idx_t>(adjncy.size());
    }

    // Check if graph has any edges
    if (adjncy.empty()) {
        std::cerr << "METIS: Graph has no edges, cannot partition" << std::endl;
        return false;
    }

    // METIS parameters
    idx_t nvtxs = static_cast<idx_t>(numVertices);
    idx_t ncon = 1;  // Number of balancing constraints
    idx_t nparts = static_cast<idx_t>(options.targetPartitions);
    idx_t edgecut = 0;

    // Output partition
    std::vector<idx_t> part(numVertices);

    // METIS options - use minimal options to reduce crash risk
    idx_t metisOptions[METIS_NOPTIONS];
    METIS_SetDefaultOptions(metisOptions);
    metisOptions[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;  // Minimize edge cut
    metisOptions[METIS_OPTION_NITER] = 10;  // Refinement iterations

    int result;

#ifdef _WIN32
    // Use SEH-protected call on Windows
    if (!callMETISSafe(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                        &nparts, metisOptions, &edgecut, part.data())) {
        std::cerr << "METIS crashed on Windows, falling back to custom partitioner" << std::endl;
        return false;
    }
    result = s_metisResult;
#else
    // Direct call on other platforms
    result = METIS_PartGraphKway(
        &nvtxs, &ncon, xadj.data(), adjncy.data(),
        nullptr, nullptr, nullptr,
        &nparts,
        nullptr, nullptr,
        metisOptions,
        &edgecut,
        part.data()
    );
#endif

    if (result != METIS_OK) {
        std::cerr << "METIS_PartGraphKway failed with error: " << result << std::endl;
        return false;
    }

    if (options.verbose) {
        std::cout << "  METIS edge cut: " << edgecut << std::endl;
    }

    // Convert to output format
    outPartition.resize(numVertices);
    for (uint32_t v = 0; v < numVertices; v++) {
        outPartition[v] = static_cast<uint32_t>(part[v]);
    }

    return true;
}

bool GraphPartitioner::partitionMETISSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                                              const std::vector<glm::vec3>& positions,
                                              uint32_t numVertices,
                                              const GraphPartitionerOptions& options,
                                              std::vector<uint32_t>& outPartition) {
    // METIS already produces high-quality, compact partitions
    // No need for post-processing - it handles 3D meshes correctly
    return partitionMETIS(adjacency, numVertices, options, outPartition);
}

#endif // USE_METIS_LIBRARY

// ============================================================================
// GraphPartitioner Constructor/Destructor
// ============================================================================

GraphPartitioner::GraphPartitioner() {}

GraphPartitioner::~GraphPartitioner() {}

bool GraphPartitioner::partition(const std::vector<std::vector<uint32_t>>& adjacency,
                                  uint32_t numVertices,
                                  const GraphPartitionerOptions& options,
                                  std::vector<uint32_t>& outPartition) {
    if (numVertices == 0) {
        return false;
    }

    if (options.targetPartitions <= 1) {
        outPartition.assign(numVertices, 0);
        return true;
    }

    if (options.targetPartitions >= numVertices) {
        // Each vertex is its own partition
        outPartition.resize(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) {
            outPartition[i] = i;
        }
        return true;
    }

    if (options.verbose) {
        std::cout << "GraphPartitioner: Partitioning " << numVertices
                  << " vertices into " << options.targetPartitions << " parts" << std::endl;
    }

    // Initialize weights (each vertex represents 1 triangle initially)
    std::vector<uint32_t> weights(numVertices, 1);

    // =========================================================================
    // Phase 1: Coarsening
    // =========================================================================
    m_CoarseLevels.clear();

    std::vector<std::vector<uint32_t>> currentAdj = adjacency;
    std::vector<uint32_t> currentWeights = weights;
    uint32_t currentVertices = numVertices;

    // Store the mapping chain for uncoarsening
    std::vector<std::vector<uint32_t>> mappingChain;

    uint32_t level = 0;
    uint32_t minCoarseSize = options.targetPartitions * 20;  // Stop when small enough

    while (currentVertices > minCoarseSize && level < options.maxCoarsenLevel) {
        std::vector<std::vector<uint32_t>> coarseAdj;
        std::vector<uint32_t> coarseWeights;
        std::vector<uint32_t> mapping;
        uint32_t coarseVertices;

        coarsenGraph(currentAdj, currentWeights, currentVertices,
                     coarseAdj, coarseWeights, mapping, coarseVertices);

        // Stop if we didn't reduce enough
        if (coarseVertices >= currentVertices * 0.9f) {
            break;
        }

        mappingChain.push_back(mapping);

        currentAdj = std::move(coarseAdj);
        currentWeights = std::move(coarseWeights);
        currentVertices = coarseVertices;
        level++;

        if (options.verbose) {
            std::cout << "  Level " << level << ": " << currentVertices << " vertices" << std::endl;
        }
    }

    if (options.verbose) {
        std::cout << "  Coarsened to " << currentVertices << " vertices in "
                  << level << " levels" << std::endl;
    }

    // =========================================================================
    // Phase 2: Initial Partitioning
    // =========================================================================
    std::vector<uint32_t> partition;
    initialPartition(currentAdj, currentWeights, currentVertices,
                     options.targetPartitions, partition);

    if (options.verbose) {
        uint32_t edgeCut = computeEdgeCut(currentAdj, partition);
        std::cout << "  Initial partition edge cut: " << edgeCut << std::endl;
    }

    // =========================================================================
    // Phase 3: Uncoarsening with Refinement
    // =========================================================================
    for (int lvl = static_cast<int>(mappingChain.size()) - 1; lvl >= 0; lvl--) {
        // Get the adjacency for this level (we need to reconstruct it)
        // For simplicity, we'll just project and refine at the finest level
    }

    // Project all the way back to the finest level
    std::vector<uint32_t> currentPartition = partition;
    for (int lvl = static_cast<int>(mappingChain.size()) - 1; lvl >= 0; lvl--) {
        const auto& mapping = mappingChain[lvl];
        uint32_t fineSize = static_cast<uint32_t>(mapping.size());

        std::vector<uint32_t> finePartition(fineSize);
        projectPartition(currentPartition, mapping, fineSize, finePartition);
        currentPartition = std::move(finePartition);
    }

    // Final refinement at finest level
    refinePartition(adjacency, weights, numVertices,
                    options.targetPartitions, currentPartition, options.refinementPasses);

    // Ensure each partition is a connected component (fixes scattered clusters)
    ensureConnectedPartitions(adjacency, currentPartition, numVertices);

    // Balance and merge small partitions
    balancePartitions(adjacency, weights, numVertices,
                      options.targetPartitions, currentPartition);

    if (options.minPartitionSize > 0) {
        mergeSmallPartitions(adjacency, currentPartition, numVertices, options.minPartitionSize);
    }

    // Re-check connectivity after merging
    ensureConnectedPartitions(adjacency, currentPartition, numVertices);

    outPartition = std::move(currentPartition);

    if (options.verbose) {
        uint32_t edgeCut = computeEdgeCut(adjacency, outPartition);
        std::cout << "  Final edge cut: " << edgeCut << std::endl;
    }

    return true;
}

bool GraphPartitioner::partitionSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                                         const std::vector<glm::vec3>& positions,
                                         uint32_t numVertices,
                                         const GraphPartitionerOptions& options,
                                         std::vector<uint32_t>& outPartition) {
    if (numVertices == 0) {
        return false;
    }

    if (options.targetPartitions <= 1) {
        outPartition.assign(numVertices, 0);
        return true;
    }

    if (options.targetPartitions >= numVertices) {
        outPartition.resize(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) {
            outPartition[i] = i;
        }
        return true;
    }

    if (options.verbose) {
        std::cout << "GraphPartitioner: Spatial partitioning " << numVertices
                  << " vertices into " << options.targetPartitions << " parts" << std::endl;
    }

    // Initialize weights
    std::vector<uint32_t> weights(numVertices, 1);

    // Coarsening phase (same as regular partition)
    m_CoarseLevels.clear();

    std::vector<std::vector<uint32_t>> currentAdj = adjacency;
    std::vector<uint32_t> currentWeights = weights;
    std::vector<glm::vec3> currentPositions = positions;
    uint32_t currentVertices = numVertices;

    std::vector<std::vector<uint32_t>> mappingChain;

    uint32_t level = 0;
    uint32_t minCoarseSize = options.targetPartitions * 20;

    while (currentVertices > minCoarseSize && level < options.maxCoarsenLevel) {
        std::vector<std::vector<uint32_t>> coarseAdj;
        std::vector<uint32_t> coarseWeights;
        std::vector<uint32_t> mapping;
        uint32_t coarseVertices;

        coarsenGraph(currentAdj, currentWeights, currentVertices,
                     coarseAdj, coarseWeights, mapping, coarseVertices);

        if (coarseVertices >= currentVertices * 0.9f) {
            break;
        }

        // Coarsen positions (average of merged vertices)
        std::vector<glm::vec3> coarsePositions(coarseVertices, glm::vec3(0.0f));
        std::vector<uint32_t> positionCounts(coarseVertices, 0);
        for (uint32_t v = 0; v < currentVertices; v++) {
            uint32_t cv = mapping[v];
            coarsePositions[cv] += currentPositions[v];
            positionCounts[cv]++;
        }
        for (uint32_t cv = 0; cv < coarseVertices; cv++) {
            if (positionCounts[cv] > 0) {
                coarsePositions[cv] /= static_cast<float>(positionCounts[cv]);
            }
        }

        mappingChain.push_back(mapping);

        currentAdj = std::move(coarseAdj);
        currentWeights = std::move(coarseWeights);
        currentPositions = std::move(coarsePositions);
        currentVertices = coarseVertices;
        level++;

        if (options.verbose) {
            std::cout << "  Level " << level << ": " << currentVertices << " vertices" << std::endl;
        }
    }

    if (options.verbose) {
        std::cout << "  Coarsened to " << currentVertices << " vertices in "
                  << level << " levels" << std::endl;
    }

    // Initial partitioning with SPATIAL seed selection
    std::vector<uint32_t> partition;
    initialPartitionSpatial(currentAdj, currentWeights, currentPositions, currentVertices,
                            options.targetPartitions, partition);

    if (options.verbose) {
        uint32_t edgeCut = computeEdgeCut(currentAdj, partition);
        std::cout << "  Initial partition edge cut: " << edgeCut << std::endl;
    }

    // Uncoarsening with refinement
    std::vector<uint32_t> currentPartition = partition;
    for (int lvl = static_cast<int>(mappingChain.size()) - 1; lvl >= 0; lvl--) {
        const auto& mapping = mappingChain[lvl];
        uint32_t fineSize = static_cast<uint32_t>(mapping.size());

        std::vector<uint32_t> finePartition(fineSize);
        projectPartition(currentPartition, mapping, fineSize, finePartition);
        currentPartition = std::move(finePartition);
    }

    // Final refinement
    refinePartition(adjacency, weights, numVertices,
                    options.targetPartitions, currentPartition, options.refinementPasses);

    // Balance and merge small partitions
    balancePartitions(adjacency, weights, numVertices,
                      options.targetPartitions, currentPartition);

    if (options.minPartitionSize > 0) {
        mergeSmallPartitions(adjacency, currentPartition, numVertices, options.minPartitionSize);
    }

    // Fix elongated partitions by splitting them along longest axis
    // This prevents "long thin pieces sticking out" artifacts
    fixElongatedPartitions(adjacency, positions, currentPartition, numVertices, 3.0f);

    outPartition = std::move(currentPartition);

    if (options.verbose) {
        uint32_t edgeCut = computeEdgeCut(adjacency, outPartition);
        std::cout << "  Final edge cut: " << edgeCut << std::endl;
    }

    return true;
}

std::vector<uint32_t> GraphPartitioner::selectSpatialSeeds(const std::vector<glm::vec3>& positions,
                                                            uint32_t numVertices,
                                                            uint32_t k) {
    // k-means++ seed selection: pick seeds that are far apart spatially
    std::vector<uint32_t> seeds;
    if (numVertices == 0 || k == 0) return seeds;

    seeds.reserve(k);

    // First seed: pick vertex closest to centroid (center of mesh)
    glm::vec3 centroid(0.0f);
    for (uint32_t i = 0; i < numVertices; i++) {
        centroid += positions[i];
    }
    centroid /= static_cast<float>(numVertices);

    uint32_t firstSeed = 0;
    float minDist = glm::length(positions[0] - centroid);
    for (uint32_t i = 1; i < numVertices; i++) {
        float d = glm::length(positions[i] - centroid);
        if (d < minDist) {
            minDist = d;
            firstSeed = i;
        }
    }
    seeds.push_back(firstSeed);

    // Subsequent seeds: pick vertex farthest from all existing seeds
    std::vector<float> minDistToSeeds(numVertices, FLT_MAX);

    for (uint32_t s = 1; s < k; s++) {
        // Update minimum distances based on last added seed
        uint32_t lastSeed = seeds.back();
        for (uint32_t i = 0; i < numVertices; i++) {
            float d = glm::length(positions[i] - positions[lastSeed]);
            minDistToSeeds[i] = std::min(minDistToSeeds[i], d);
        }

        // Find vertex with maximum minimum distance to any seed
        uint32_t bestVertex = 0;
        float maxMinDist = -1.0f;
        for (uint32_t i = 0; i < numVertices; i++) {
            // Skip already selected seeds
            bool isSeed = false;
            for (uint32_t seed : seeds) {
                if (i == seed) {
                    isSeed = true;
                    break;
                }
            }
            if (isSeed) continue;

            if (minDistToSeeds[i] > maxMinDist) {
                maxMinDist = minDistToSeeds[i];
                bestVertex = i;
            }
        }

        seeds.push_back(bestVertex);
    }

    return seeds;
}

void GraphPartitioner::initialPartitionSpatial(const std::vector<std::vector<uint32_t>>& adjacency,
                                                const std::vector<uint32_t>& weights,
                                                const std::vector<glm::vec3>& positions,
                                                uint32_t numVertices,
                                                uint32_t numPartitions,
                                                std::vector<uint32_t>& partition) {
    // Spatial-aware partitioning: use k-means++ to select well-distributed seeds
    // Then grow partitions from each seed using BFS

    partition.resize(numVertices);
    std::fill(partition.begin(), partition.end(), UINT32_MAX);

    uint32_t totalWeight = 0;
    for (uint32_t w : weights) {
        totalWeight += w;
    }
    uint32_t targetPartSize = (totalWeight + numPartitions - 1) / numPartitions;

    std::vector<uint32_t> partitionWeights(numPartitions, 0);
    uint32_t assigned = 0;

    // Select spatially distributed seeds using k-means++
    std::vector<uint32_t> seeds = selectSpatialSeeds(positions, numVertices, numPartitions);

    // Grow partitions from each seed simultaneously (multi-source BFS)
    // This ensures more balanced, compact partitions

    using PQEntry = std::pair<float, std::pair<uint32_t, uint32_t>>;  // (distance, (vertex, partition))
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> frontier;

    // Initialize frontier with all seeds
    for (uint32_t p = 0; p < seeds.size() && p < numPartitions; p++) {
        uint32_t seed = seeds[p];
        partition[seed] = p;
        partitionWeights[p] += weights[seed];
        assigned++;

        // Add neighbors to frontier
        for (uint32_t neighbor : adjacency[seed]) {
            if (partition[neighbor] == UINT32_MAX) {
                float dist = glm::length(positions[neighbor] - positions[seed]);
                frontier.push({dist, {neighbor, p}});
            }
        }
    }

    // Grow all partitions simultaneously
    while (!frontier.empty() && assigned < numVertices) {
        auto [dist, vp] = frontier.top();
        frontier.pop();

        uint32_t v = vp.first;
        uint32_t p = vp.second;

        // Skip if already assigned
        if (partition[v] != UINT32_MAX) continue;

        // Skip if this partition is already at capacity
        if (partitionWeights[p] >= targetPartSize) {
            // Try to reassign to a neighbor partition with capacity
            bool reassigned = false;
            for (uint32_t neighbor : adjacency[v]) {
                if (partition[neighbor] != UINT32_MAX && partition[neighbor] != p) {
                    uint32_t np = partition[neighbor];
                    if (partitionWeights[np] < targetPartSize) {
                        p = np;
                        reassigned = true;
                        break;
                    }
                }
            }
            if (!reassigned && partitionWeights[p] >= targetPartSize * 1.2f) {
                continue;  // Skip this vertex for now
            }
        }

        // Assign vertex to partition
        partition[v] = p;
        partitionWeights[p] += weights[v];
        assigned++;

        // Add unassigned neighbors to frontier
        for (uint32_t neighbor : adjacency[v]) {
            if (partition[neighbor] == UINT32_MAX) {
                float d = glm::length(positions[neighbor] - positions[seeds[p]]);
                frontier.push({d, {neighbor, p}});
            }
        }
    }

    // Assign any remaining unassigned vertices
    for (uint32_t v = 0; v < numVertices; v++) {
        if (partition[v] == UINT32_MAX) {
            // Find nearest partition by checking neighbors
            uint32_t bestPart = 0;
            float bestDist = FLT_MAX;

            for (uint32_t n : adjacency[v]) {
                if (partition[n] != UINT32_MAX) {
                    float d = glm::length(positions[v] - positions[seeds[partition[n]]]);
                    if (d < bestDist) {
                        bestDist = d;
                        bestPart = partition[n];
                    }
                }
            }

            // If no neighbors assigned, find nearest seed
            if (bestDist == FLT_MAX) {
                for (uint32_t p = 0; p < seeds.size(); p++) {
                    float d = glm::length(positions[v] - positions[seeds[p]]);
                    if (d < bestDist) {
                        bestDist = d;
                        bestPart = p;
                    }
                }
            }

            partition[v] = bestPart;
            partitionWeights[bestPart] += weights[v];
        }
    }
}

void GraphPartitioner::coarsenGraph(const std::vector<std::vector<uint32_t>>& fineAdj,
                                     const std::vector<uint32_t>& fineWeights,
                                     uint32_t fineVertices,
                                     std::vector<std::vector<uint32_t>>& coarseAdj,
                                     std::vector<uint32_t>& coarseWeights,
                                     std::vector<uint32_t>& mapping,
                                     uint32_t& coarseVertices) {
    // Heavy Edge Matching (HEM) - deterministic version
    // Match vertices with their heaviest unmatched neighbor
    // Process in sequential order to maintain spatial coherence

    mapping.resize(fineVertices);
    std::fill(mapping.begin(), mapping.end(), UINT32_MAX);

    std::vector<bool> matched(fineVertices, false);
    coarseVertices = 0;

    // Process vertices in sequential order (preserves spatial locality from adjacency)
    for (uint32_t v = 0; v < fineVertices; v++) {
        if (matched[v]) continue;

        // Find unmatched neighbor with highest connectivity (most shared neighbors)
        uint32_t bestNeighbor = UINT32_MAX;
        uint32_t bestScore = 0;

        for (uint32_t neighbor : fineAdj[v]) {
            if (!matched[neighbor] && neighbor != v) {
                // Score: number of shared neighbors (creates better coarse vertices)
                uint32_t sharedCount = 0;
                for (uint32_t nn : fineAdj[neighbor]) {
                    for (uint32_t vn : fineAdj[v]) {
                        if (nn == vn) {
                            sharedCount++;
                            break;
                        }
                    }
                }
                // Tie-break by lower index (deterministic)
                if (sharedCount > bestScore ||
                    (sharedCount == bestScore && (bestNeighbor == UINT32_MAX || neighbor < bestNeighbor))) {
                    bestScore = sharedCount;
                    bestNeighbor = neighbor;
                }
            }
        }

        if (bestNeighbor != UINT32_MAX) {
            // Match v with bestNeighbor
            mapping[v] = coarseVertices;
            mapping[bestNeighbor] = coarseVertices;
            matched[v] = true;
            matched[bestNeighbor] = true;
        } else {
            // No match found, v becomes its own coarse vertex
            mapping[v] = coarseVertices;
            matched[v] = true;
        }
        coarseVertices++;
    }

    // Build coarse graph
    coarseAdj.resize(coarseVertices);
    coarseWeights.resize(coarseVertices, 0);

    // Accumulate weights
    for (uint32_t v = 0; v < fineVertices; v++) {
        coarseWeights[mapping[v]] += fineWeights[v];
    }

    // Build coarse adjacency
    for (uint32_t v = 0; v < fineVertices; v++) {
        uint32_t cv = mapping[v];
        for (uint32_t neighbor : fineAdj[v]) {
            uint32_t cn = mapping[neighbor];
            if (cv != cn) {
                // Add edge if not already present
                auto& neighbors = coarseAdj[cv];
                if (std::find(neighbors.begin(), neighbors.end(), cn) == neighbors.end()) {
                    neighbors.push_back(cn);
                }
            }
        }
    }
}

void GraphPartitioner::initialPartition(const std::vector<std::vector<uint32_t>>& adjacency,
                                         const std::vector<uint32_t>& weights,
                                         uint32_t numVertices,
                                         uint32_t numPartitions,
                                         std::vector<uint32_t>& partition) {
    // Greedy Graph Growing Partitioning (GGGP)
    // Grow partitions using BFS to create spatially connected regions

    partition.resize(numVertices);
    std::fill(partition.begin(), partition.end(), UINT32_MAX);

    uint32_t totalWeight = 0;
    for (uint32_t w : weights) {
        totalWeight += w;
    }
    uint32_t targetPartSize = (totalWeight + numPartitions - 1) / numPartitions;

    std::vector<uint32_t> partitionWeights(numPartitions, 0);
    uint32_t assigned = 0;

    // Use priority queue to always expand from boundary vertices
    // This creates more compact, connected partitions
    using PQEntry = std::pair<int, uint32_t>;  // (negative priority, vertex)

    for (uint32_t p = 0; p < numPartitions && assigned < numVertices; p++) {
        // Find seed vertex - prefer vertex far from already assigned vertices
        uint32_t seed = UINT32_MAX;
        int bestScore = -1;

        for (uint32_t v = 0; v < numVertices; v++) {
            if (partition[v] != UINT32_MAX) continue;

            // Score: number of unassigned neighbors (prefer interior vertices as seeds)
            int score = 0;
            for (uint32_t n : adjacency[v]) {
                if (partition[n] == UINT32_MAX) {
                    score++;
                }
            }

            // For first partition, just pick any vertex
            // For subsequent partitions, prefer vertices with NO assigned neighbors
            // (i.e., far from existing partitions)
            if (p > 0) {
                bool hasAssignedNeighbor = false;
                for (uint32_t n : adjacency[v]) {
                    if (partition[n] != UINT32_MAX) {
                        hasAssignedNeighbor = true;
                        break;
                    }
                }
                if (!hasAssignedNeighbor) {
                    score += 1000;  // Strong preference for isolated vertices
                }
            }

            if (score > bestScore) {
                bestScore = score;
                seed = v;
            }
        }

        if (seed == UINT32_MAX) break;

        // Grow partition from seed using priority BFS
        // Priority = number of neighbors already in this partition (higher = better)
        std::priority_queue<PQEntry> frontier;
        frontier.push({0, seed});
        partition[seed] = p;
        partitionWeights[p] += weights[seed];
        assigned++;

        while (!frontier.empty() && partitionWeights[p] < targetPartSize) {
            uint32_t v = frontier.top().second;
            frontier.pop();

            for (uint32_t neighbor : adjacency[v]) {
                if (partition[neighbor] == UINT32_MAX) {
                    partition[neighbor] = p;
                    partitionWeights[p] += weights[neighbor];
                    assigned++;

                    // Calculate priority for this vertex's neighbors
                    int priority = 0;
                    for (uint32_t nn : adjacency[neighbor]) {
                        if (partition[nn] == p) {
                            priority++;
                        }
                    }
                    frontier.push({priority, neighbor});

                    if (partitionWeights[p] >= targetPartSize) {
                        break;
                    }
                }
            }
        }
    }

    // Assign any remaining unassigned vertices to neighbor partition (prefer connected)
    for (uint32_t v = 0; v < numVertices; v++) {
        if (partition[v] == UINT32_MAX) {
            // Find partition with most neighbors to this vertex
            std::vector<uint32_t> neighborCount(numPartitions, 0);
            for (uint32_t n : adjacency[v]) {
                if (partition[n] != UINT32_MAX && partition[n] < numPartitions) {
                    neighborCount[partition[n]]++;
                }
            }

            uint32_t bestPart = 0;
            uint32_t bestCount = neighborCount[0];
            for (uint32_t p = 1; p < numPartitions; p++) {
                // Prefer partition with more neighbors, tie-break by smaller size
                if (neighborCount[p] > bestCount ||
                    (neighborCount[p] == bestCount && partitionWeights[p] < partitionWeights[bestPart])) {
                    bestCount = neighborCount[p];
                    bestPart = p;
                }
            }

            partition[v] = bestPart;
            partitionWeights[bestPart] += weights[v];
        }
    }
}

void GraphPartitioner::projectPartition(const std::vector<uint32_t>& coarsePartition,
                                         const std::vector<uint32_t>& mapping,
                                         uint32_t fineVertices,
                                         std::vector<uint32_t>& finePartition) {
    finePartition.resize(fineVertices);
    for (uint32_t v = 0; v < fineVertices; v++) {
        finePartition[v] = coarsePartition[mapping[v]];
    }
}

void GraphPartitioner::refinePartition(const std::vector<std::vector<uint32_t>>& adjacency,
                                        const std::vector<uint32_t>& weights,
                                        uint32_t numVertices,
                                        uint32_t numPartitions,
                                        std::vector<uint32_t>& partition,
                                        uint32_t maxPasses) {
    // Simplified Kernighan-Lin / Fiduccia-Mattheyses refinement
    // Move boundary vertices to reduce edge cut while maintaining balance

    if (numVertices == 0 || numPartitions <= 1) return;
    if (partition.size() < numVertices) return;  // Safety check
    if (adjacency.size() < numVertices) return;  // Safety check

    std::vector<uint32_t> partitionWeights(numPartitions, 0);
    computePartitionSizes(partition, weights, numPartitions, partitionWeights);

    uint32_t totalWeight = 0;
    for (uint32_t w : partitionWeights) {
        totalWeight += w;
    }
    if (totalWeight == 0) return;  // Safety check

    uint32_t avgWeight = totalWeight / numPartitions;
    uint32_t maxImbalance = std::max(avgWeight / 5, 1u);  // Allow 20% imbalance for better quality

    for (uint32_t pass = 0; pass < maxPasses; pass++) {
        bool improved = false;
        uint32_t movesMade = 0;

        for (uint32_t v = 0; v < numVertices; v++) {
            if (v >= partition.size()) continue;  // Bounds check
            uint32_t currentPart = partition[v];
            if (currentPart >= numPartitions) continue;  // Invalid partition

            // Count edges to each partition
            std::vector<uint32_t> edgesToPart(numPartitions, 0);
            uint32_t totalEdges = 0;
            for (uint32_t neighbor : adjacency[v]) {
                if (neighbor >= partition.size()) continue;  // Bounds check
                uint32_t neighborPart = partition[neighbor];
                if (neighborPart >= numPartitions) continue;  // Invalid partition
                edgesToPart[neighborPart]++;
                totalEdges++;
            }

            // Skip if vertex has no edges
            if (totalEdges == 0) continue;

            // Current internal edges (edges to same partition)
            uint32_t internalEdges = edgesToPart[currentPart];

            // Skip if all neighbors are in same partition (no cut edges)
            if (internalEdges == totalEdges) continue;

            // Find best partition to move to (one with MOST neighbors)
            uint32_t bestPart = currentPart;
            uint32_t bestEdges = internalEdges;

            for (uint32_t p = 0; p < numPartitions; p++) {
                if (p == currentPart) continue;
                if (edgesToPart[p] == 0) continue;  // No neighbors in this partition

                // Check balance constraint
                if (partitionWeights[p] + weights[v] > avgWeight + maxImbalance) {
                    continue;
                }
                if (partitionWeights[currentPart] - weights[v] < avgWeight - maxImbalance) {
                    continue;
                }

                // Move to partition with MORE neighbors (reduces cut edges)
                if (edgesToPart[p] > bestEdges) {
                    bestEdges = edgesToPart[p];
                    bestPart = p;
                }
            }

            // Move if we found a better partition
            if (bestPart != currentPart) {
                partitionWeights[currentPart] -= weights[v];
                partitionWeights[bestPart] += weights[v];
                partition[v] = bestPart;
                improved = true;
                movesMade++;
            }
        }

        if (!improved || movesMade == 0) break;
    }
}

uint32_t GraphPartitioner::computeEdgeCut(const std::vector<std::vector<uint32_t>>& adjacency,
                                           const std::vector<uint32_t>& partition) const {
    uint32_t cut = 0;
    for (size_t v = 0; v < adjacency.size(); v++) {
        for (uint32_t neighbor : adjacency[v]) {
            if (partition[v] != partition[neighbor]) {
                cut++;
            }
        }
    }
    return cut / 2;  // Each edge counted twice
}

void GraphPartitioner::computePartitionSizes(const std::vector<uint32_t>& partition,
                                              const std::vector<uint32_t>& weights,
                                              uint32_t numPartitions,
                                              std::vector<uint32_t>& sizes) const {
    sizes.assign(numPartitions, 0);
    for (size_t v = 0; v < partition.size(); v++) {
        if (partition[v] < numPartitions) {
            sizes[partition[v]] += weights[v];
        }
    }
}

void GraphPartitioner::balancePartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                          const std::vector<uint32_t>& weights,
                                          uint32_t numVertices,
                                          uint32_t numPartitions,
                                          std::vector<uint32_t>& partition) {
    // Move vertices from overweight partitions to underweight ones
    if (numVertices == 0 || numPartitions == 0) return;
    if (partition.size() < numVertices) return;
    if (weights.size() < numVertices) return;

    std::vector<uint32_t> partitionWeights(numPartitions, 0);
    computePartitionSizes(partition, weights, numPartitions, partitionWeights);

    uint32_t totalWeight = 0;
    for (uint32_t w : partitionWeights) {
        totalWeight += w;
    }
    if (totalWeight == 0) return;
    uint32_t avgWeight = totalWeight / numPartitions;

    // Find overweight and underweight partitions
    for (uint32_t v = 0; v < numVertices; v++) {
        if (v >= partition.size()) continue;
        uint32_t p = partition[v];
        if (p >= numPartitions) continue;
        if (partitionWeights[p] <= avgWeight) continue;

        // Find underweight neighbor partition
        if (v >= adjacency.size()) continue;
        for (uint32_t neighbor : adjacency[v]) {
            if (neighbor >= partition.size()) continue;
            uint32_t np = partition[neighbor];
            if (np >= numPartitions) continue;
            if (np != p && partitionWeights[np] < avgWeight) {
                // Move v to np
                if (v < weights.size()) {
                    partitionWeights[p] -= weights[v];
                    partitionWeights[np] += weights[v];
                }
                partition[v] = np;
                break;
            }
        }
    }
}

void GraphPartitioner::mergeSmallPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                             std::vector<uint32_t>& partition,
                                             uint32_t numVertices,
                                             uint32_t minSize) {
    if (numVertices == 0) return;
    if (partition.size() < numVertices) return;

    // Count partition sizes
    std::unordered_map<uint32_t, uint32_t> partSizes;
    for (uint32_t v = 0; v < numVertices; v++) {
        if (v < partition.size()) {
            partSizes[partition[v]]++;
        }
    }

    // Find small partitions and their most common neighbor partition
    for (auto it = partSizes.begin(); it != partSizes.end(); ++it) {
        uint32_t partId = it->first;
        uint32_t size = it->second;

        if (size >= minSize) continue;

        // Find most connected neighbor partition
        std::unordered_map<uint32_t, uint32_t> neighborCounts;
        for (uint32_t v = 0; v < numVertices; v++) {
            if (v >= partition.size()) continue;
            if (partition[v] != partId) continue;
            if (v >= adjacency.size()) continue;

            for (uint32_t neighbor : adjacency[v]) {
                if (neighbor >= partition.size()) continue;
                uint32_t np = partition[neighbor];
                if (np != partId) {
                    neighborCounts[np]++;
                }
            }
        }

        if (neighborCounts.empty()) continue;

        // Find best neighbor partition
        uint32_t bestNeighbor = partId;
        uint32_t bestCount = 0;
        for (auto nit = neighborCounts.begin(); nit != neighborCounts.end(); ++nit) {
            if (nit->second > bestCount) {
                bestCount = nit->second;
                bestNeighbor = nit->first;
            }
        }

        // Merge into neighbor
        if (bestNeighbor != partId) {
            for (uint32_t v = 0; v < numVertices; v++) {
                if (partition[v] == partId) {
                    partition[v] = bestNeighbor;
                }
            }
            partSizes[bestNeighbor] += size;
            it->second = 0;
        }
    }

    // Renumber partitions to be contiguous
    std::unordered_map<uint32_t, uint32_t> remap;
    uint32_t nextId = 0;
    for (uint32_t v = 0; v < numVertices; v++) {
        uint32_t p = partition[v];
        if (remap.find(p) == remap.end()) {
            remap[p] = nextId++;
        }
        partition[v] = remap[p];
    }
}

void GraphPartitioner::ensureConnectedPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                                   std::vector<uint32_t>& partition,
                                                   uint32_t numVertices) {
    if (numVertices == 0) return;
    if (partition.size() < numVertices) return;
    if (adjacency.size() < numVertices) return;

    // Find all unique partitions
    std::unordered_set<uint32_t> uniquePartitions;
    for (uint32_t v = 0; v < numVertices; v++) {
        if (v < partition.size()) {
            uniquePartitions.insert(partition[v]);
        }
    }

    uint32_t nextNewPartition = 0;
    for (uint32_t p : uniquePartitions) {
        nextNewPartition = std::max(nextNewPartition, p + 1);
    }

    // For each partition, check if it's connected
    for (uint32_t p : uniquePartitions) {
        // Find all vertices in this partition
        std::vector<uint32_t> partVertices;
        for (uint32_t v = 0; v < numVertices; v++) {
            if (partition[v] == p) {
                partVertices.push_back(v);
            }
        }

        if (partVertices.empty()) continue;

        // BFS to find connected components within this partition
        std::vector<bool> visited(numVertices, false);
        std::vector<std::vector<uint32_t>> components;

        for (uint32_t startV : partVertices) {
            if (visited[startV]) continue;

            // BFS from startV
            std::vector<uint32_t> component;
            std::queue<uint32_t> queue;
            queue.push(startV);
            visited[startV] = true;

            while (!queue.empty()) {
                uint32_t v = queue.front();
                queue.pop();
                component.push_back(v);

                if (v >= adjacency.size()) continue;
                for (uint32_t neighbor : adjacency[v]) {
                    if (neighbor >= numVertices) continue;
                    if (neighbor >= partition.size()) continue;
                    if (!visited[neighbor] && partition[neighbor] == p) {
                        visited[neighbor] = true;
                        queue.push(neighbor);
                    }
                }
            }

            components.push_back(std::move(component));
        }

        // If there's more than one component, keep the largest and merge others to neighbors
        if (components.size() > 1) {
            // Find largest component
            size_t largestIdx = 0;
            size_t largestSize = components[0].size();
            for (size_t i = 1; i < components.size(); i++) {
                if (components[i].size() > largestSize) {
                    largestSize = components[i].size();
                    largestIdx = i;
                }
            }

            // Merge smaller components into their neighbor partitions
            for (size_t i = 0; i < components.size(); i++) {
                if (i == largestIdx) continue;

                for (uint32_t v : components[i]) {
                    if (v >= adjacency.size()) continue;
                    // Find neighbor partition with most connections
                    std::unordered_map<uint32_t, uint32_t> neighborCounts;
                    for (uint32_t neighbor : adjacency[v]) {
                        if (neighbor >= partition.size()) continue;
                        uint32_t np = partition[neighbor];
                        if (np != p) {
                            neighborCounts[np]++;
                        }
                    }

                    // Find best neighbor (with most connections)
                    uint32_t bestPart = p;  // Keep in same partition as fallback
                    uint32_t bestCount = 0;
                    for (const auto& [np, count] : neighborCounts) {
                        if (count > bestCount) {
                            bestCount = count;
                            bestPart = np;
                        }
                    }

                    // If no neighbor found, assign to a new partition
                    if (bestPart == p && neighborCounts.empty()) {
                        bestPart = nextNewPartition++;
                    }

                    if (v < partition.size()) {
                        partition[v] = bestPart;
                    }
                }
            }
        }
    }

    // Renumber partitions to be contiguous
    std::unordered_map<uint32_t, uint32_t> remap;
    uint32_t nextId = 0;
    for (uint32_t v = 0; v < numVertices; v++) {
        uint32_t p = partition[v];
        if (remap.find(p) == remap.end()) {
            remap[p] = nextId++;
        }
        partition[v] = remap[p];
    }
}

void GraphPartitioner::fixElongatedPartitions(const std::vector<std::vector<uint32_t>>& adjacency,
                                               const std::vector<glm::vec3>& positions,
                                               std::vector<uint32_t>& partition,
                                               uint32_t numVertices,
                                               float maxAspectRatio) {
    if (numVertices == 0 || positions.size() < numVertices) return;
    if (partition.size() < numVertices) return;

    // Find all unique partitions
    std::unordered_set<uint32_t> uniquePartitions;
    for (uint32_t v = 0; v < numVertices; v++) {
        uniquePartitions.insert(partition[v]);
    }

    uint32_t nextPartitionId = 0;
    for (uint32_t p : uniquePartitions) {
        nextPartitionId = std::max(nextPartitionId, p + 1);
    }

    bool madeChanges = true;
    int iterations = 0;
    const int maxIterations = 10;  // Prevent infinite loops

    while (madeChanges && iterations < maxIterations) {
        madeChanges = false;
        iterations++;

        // Re-gather unique partitions (may have changed)
        uniquePartitions.clear();
        for (uint32_t v = 0; v < numVertices; v++) {
            uniquePartitions.insert(partition[v]);
        }

        for (uint32_t p : uniquePartitions) {
            // Gather all vertices in this partition
            std::vector<uint32_t> partVerts;
            for (uint32_t v = 0; v < numVertices; v++) {
                if (partition[v] == p) {
                    partVerts.push_back(v);
                }
            }

            if (partVerts.size() < 4) continue;  // Too small to split

            // Compute bounding box
            glm::vec3 minBounds(FLT_MAX);
            glm::vec3 maxBounds(-FLT_MAX);
            for (uint32_t v : partVerts) {
                minBounds = glm::min(minBounds, positions[v]);
                maxBounds = glm::max(maxBounds, positions[v]);
            }

            glm::vec3 extent = maxBounds - minBounds;

            // Find longest and shortest axes
            float maxExtent = std::max({extent.x, extent.y, extent.z});
            float minExtent = std::min({extent.x, extent.y, extent.z});

            // Handle 2D case (e.g., flat plane where one axis is ~0)
            if (minExtent < 0.0001f) {
                // Find the two non-zero extents
                std::vector<float> nonZeroExtents;
                if (extent.x > 0.0001f) nonZeroExtents.push_back(extent.x);
                if (extent.y > 0.0001f) nonZeroExtents.push_back(extent.y);
                if (extent.z > 0.0001f) nonZeroExtents.push_back(extent.z);

                if (nonZeroExtents.size() >= 2) {
                    std::sort(nonZeroExtents.begin(), nonZeroExtents.end());
                    minExtent = nonZeroExtents[0];
                    maxExtent = nonZeroExtents.back();
                } else if (nonZeroExtents.size() == 1) {
                    // 1D line - definitely elongated
                    minExtent = 0.001f;
                    maxExtent = nonZeroExtents[0];
                } else {
                    continue;  // All zero extent, skip
                }
            }

            float aspectRatio = maxExtent / std::max(minExtent, 0.0001f);

            // Check if elongated
            if (aspectRatio > maxAspectRatio) {
                // Find the longest axis
                int longestAxis = 0;
                if (extent.y > extent.x && extent.y > extent.z) longestAxis = 1;
                else if (extent.z > extent.x && extent.z > extent.y) longestAxis = 2;

                // Sort vertices along longest axis
                std::sort(partVerts.begin(), partVerts.end(), [&](uint32_t a, uint32_t b) {
                    return positions[a][longestAxis] < positions[b][longestAxis];
                });

                // Split at median
                size_t midPoint = partVerts.size() / 2;

                // Assign second half to new partition
                uint32_t newPartId = nextPartitionId++;
                for (size_t i = midPoint; i < partVerts.size(); i++) {
                    partition[partVerts[i]] = newPartId;
                }

                madeChanges = true;
            }
        }
    }

    // Renumber partitions to be contiguous
    std::unordered_map<uint32_t, uint32_t> remap;
    uint32_t finalId = 0;
    for (uint32_t v = 0; v < numVertices; v++) {
        uint32_t p = partition[v];
        if (remap.find(p) == remap.end()) {
            remap[p] = finalId++;
        }
        partition[v] = remap[p];
    }
}

} // namespace MiEngine
