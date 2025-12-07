# Virtual Geometry System

## Overview

MiEngine2's Virtual Geometry system provides **cluster-based mesh partitioning** and **LOD hierarchy generation** for efficient rendering of high-polygon meshes. This document explains how the clustering algorithm works.

---

## Core Concepts

### What is a Cluster?

A **cluster** is a small group of ~128 triangles that forms the fundamental rendering unit. Instead of rendering individual triangles or entire meshes, the engine renders clusters which can be:
- Culled individually (frustum/occlusion)
- Swapped between LOD levels
- Streamed in/out of GPU memory

### Why Clustering?

Traditional mesh rendering has limitations:
- **Per-mesh culling** is too coarse for large objects
- **Per-triangle culling** is too expensive
- **Static LODs** cause visible popping

Clustering provides a middle ground with these benefits:
- Fine-grained GPU culling via compute shaders
- Seamless LOD transitions through the DAG hierarchy
- Memory-efficient streaming for massive scenes

---

## Algorithm Pipeline

```
Input Mesh (vertices + indices)
         │
         ▼
┌─────────────────────────────┐
│  1. Build Adjacency Graph   │  ← Triangles connected by shared edges
└─────────────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│  2. Partition into Clusters │  ← METIS or Greedy BFS
└─────────────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│  3. Create Cluster Objects  │  ← Bounds, indices, metadata
└─────────────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│  4. Build LOD Hierarchy     │  ← QEM simplification + DAG
└─────────────────────────────┘
         │
         ▼
Output: ClusteredMesh (clusters + LOD DAG)
```

---

## Step 1: Triangle Adjacency Graph

The first step builds a graph where:
- **Nodes** = triangles
- **Edges** = shared edges between triangles

### Implementation

```cpp
// For each edge in the mesh, track which triangles share it
std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTriangles;

// Edge key = min(v0,v1) * vertexCount + max(v0,v1)
auto makeEdgeKey = [](uint32_t v0, uint32_t v1) -> uint64_t {
    if (v0 > v1) std::swap(v0, v1);
    return static_cast<uint64_t>(v0) * vertexCount + v1;
};

// Two triangles are adjacent if they share an edge
for (const auto& [edge, triangles] : edgeToTriangles) {
    for (each pair of triangles sharing this edge) {
        neighbors[t0].push_back(t1);
        neighbors[t1].push_back(t0);
    }
}
```

### Example

For a simple cube face (2 triangles):
```
    v0 ─────── v1
    │ \       │
    │   \ T0  │
    │     \   │
    │  T1   \ │
    v2 ─────── v3

Edge (v0,v3) is shared → T0 and T1 are neighbors
```

---

## Step 2: Graph Partitioning

Once we have the adjacency graph, we partition triangles into clusters of ~128 triangles each.

### Method A: METIS (Optimal)

[METIS](http://glaros.dtc.umn.edu/gkhome/metis/metis/overview) is a graph partitioning library that minimizes edge cuts:

```cpp
// Convert adjacency to METIS format
idx_t nvtxs = numTriangles;      // Number of vertices (triangles)
idx_t nparts = targetClusterCount;
std::vector<idx_t> xadj, adjncy;  // CSR format

// Call METIS
METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                    nullptr, nullptr, nullptr, &nparts,
                    nullptr, nullptr, nullptr, &objval, part.data());
```

**Benefits:**
- Minimizes cluster boundaries (fewer edge cuts)
- Balanced cluster sizes
- Better cache coherency

**To enable:** Install via vcpkg (`vcpkg install metis:x64-windows`) and define `VGEO_USE_METIS`

### Method B: Greedy BFS (Fallback)

When METIS is unavailable, a greedy BFS approach is used:

```cpp
while (unassigned triangles remain) {
    // Start new cluster from first unassigned triangle
    seed = findFirstUnassigned();
    queue.push(seed);
    clusterAssignment[seed] = currentCluster;

    // BFS to grow cluster
    while (!queue.empty() && clusterSize < targetSize) {
        current = queue.front();
        queue.pop();

        for (neighbor : adjacency.neighbors[current]) {
            if (unassigned(neighbor)) {
                clusterAssignment[neighbor] = currentCluster;
                queue.push(neighbor);
                clusterSize++;
            }
        }
    }
    currentCluster++;
}
```

**How it works:**
1. Pick an unassigned triangle as a seed
2. Use BFS to add neighboring triangles until cluster reaches target size (~128)
3. Repeat for remaining triangles

**Trade-offs:**
- Simpler implementation
- May produce less balanced clusters
- Cluster shapes depend on seed selection order

---

## Step 3: Create Cluster Objects

After partitioning, each cluster gets its own:
- **Local vertex buffer** (only vertices used by this cluster)
- **Local index buffer** (indices remapped to local vertices)
- **Bounding volumes** (sphere + AABB for culling)
- **Debug color** (for visualization)

### Vertex Remapping

```cpp
// Original mesh has global indices (0 to vertexCount-1)
// Each cluster needs local indices (0 to clusterVertexCount-1)

for (triangle : clusterTriangles) {
    for (v = 0; v < 3; v++) {
        originalIdx = indices[triangle * 3 + v];

        if (originalIdx not in remap) {
            newIdx = clusterVertices.size();
            remap[originalIdx] = newIdx;
            clusterVertices.push_back(vertices[originalIdx]);
        }

        clusterIndices.push_back(remap[originalIdx]);
    }
}
```

### Bounding Volume Computation

```cpp
// Compute AABB
for (vertex : clusterVertices) {
    aabbMin = min(aabbMin, vertex.position);
    aabbMax = max(aabbMax, vertex.position);
}

// Compute bounding sphere (Ritter's algorithm)
center = (aabbMin + aabbMax) / 2;
radius = 0;
for (vertex : clusterVertices) {
    radius = max(radius, distance(center, vertex.position));
}
```

---

## Step 4: LOD Hierarchy (DAG)

The LOD system builds a **Directed Acyclic Graph** where:
- **Leaf clusters** (LOD 0) = highest detail, original triangles
- **Parent clusters** (LOD 1, 2, ...) = simplified versions
- **Root clusters** (highest LOD) = coarsest representation

### Simplification via QEM

Each LOD level is created by **merging adjacent clusters** and **simplifying the geometry** using Quadric Error Metrics (QEM):

```cpp
// QEM assigns an error matrix to each vertex based on adjacent faces
struct QuadricMatrix {
    float a[10];  // Symmetric 4x4 matrix stored as 10 floats

    // Error of placing vertex at position v
    float evaluate(vec3 v);

    // Create from triangle's plane equation
    static QuadricMatrix fromTriangle(vec3 v0, vec3 v1, vec3 v2);
};

// Edge collapse: merge two vertices into one
// Position is chosen to minimize quadric error
void collapseEdge(v0, v1) {
    QuadricMatrix combined = quadric[v0] + quadric[v1];
    vec3 optimalPosition = combined.minimizeError();
    // Replace v0 with optimal position, remove v1
}
```

### DAG Structure

```
LOD 0 (Leaves):     [C0] [C1] [C2] [C3] [C4] [C5] [C6] [C7]
                      \   /     \   /     \   /     \   /
LOD 1:                [C8]       [C9]      [C10]     [C11]
                         \       /            \       /
LOD 2:                    [C12]                [C13]
                              \                /
LOD 3 (Root):                    [C14]
```

Each parent cluster stores:
- `childClusterStart`: index of first child
- `childClusterCount`: number of children
- `lodError`: geometric error introduced by simplification

---

## Data Structures

### Cluster

```cpp
struct Cluster {
    // Identification
    uint32_t clusterId;
    uint32_t lodLevel;          // 0 = highest detail
    uint32_t meshId;

    // Geometry (offsets into global buffers)
    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t triangleCount;

    // Bounding volumes
    vec3 boundingSphereCenter;
    float boundingSphereRadius;
    vec3 aabbMin, aabbMax;

    // LOD hierarchy
    float lodError;             // Error if this cluster is used
    uint32_t parentClusterStart;
    uint32_t parentClusterCount;
    uint32_t childClusterStart;
    uint32_t childClusterCount;

    // Debug
    vec4 debugColor;            // Unique color for visualization
};
```

### ClusteredMesh

```cpp
struct ClusteredMesh {
    string name;
    vector<Cluster> clusters;    // All clusters across all LODs
    vector<ClusterVertex> vertices;
    vector<uint32_t> indices;

    // Hierarchy info
    uint32_t maxLodLevel;
    uint32_t rootClusterStart, rootClusterCount;   // Coarsest
    uint32_t leafClusterStart, leafClusterCount;   // Finest

    // Bounds
    vec3 boundingSphereCenter;
    float boundingSphereRadius;
};
```

---

## Usage Example

```cpp
#include "include/virtualgeo/MeshClusterer.h"
#include "include/virtualgeo/ClusterDAGBuilder.h"

// 1. Create a high-poly mesh
ModelLoader loader;
MeshData sphereData = loader.CreateSphere(1.0f, 32, 32);  // ~2000 triangles

// 2. Configure clustering options
ClusteringOptions options;
options.targetClusterSize = 128;    // Triangles per cluster
options.maxLodLevels = 8;
options.generateDebugColors = true;
options.verbose = true;

// 3. Run clustering
MeshClusterer clusterer;
ClusteredMesh clusteredMesh;
clusterer.clusterMesh(sphereData.vertices, indices, options, clusteredMesh);

// 4. Build LOD hierarchy
ClusterDAGBuilder dagBuilder;
dagBuilder.buildDAG(clusteredMesh, options);

// Result:
// - clusteredMesh.clusters contains all clusters at all LOD levels
// - LOD 0: ~16-18 clusters (original detail)
// - LOD 1: ~5 clusters (simplified)
// - LOD 2: ~2 clusters (coarsest)
```

---

## Debug Visualization

### Cluster Colors (Debug Layer 16)

Each cluster is assigned a unique debug color for visualization:

```cpp
// Color generation based on cluster ID
vec4 generateDebugColor(uint32_t clusterId) {
    float hue = fmod(clusterId * 0.618033988749895f, 1.0f);  // Golden ratio
    // HSV to RGB conversion...
    return vec4(r, g, b, 1.0f);
}
```

To visualize:
1. Run Virtual Geo Test mode (Mode 11)
2. Open Render Debug Panel
3. Set Debug Layer to "16: Vertex Colors (Clusters)"

### What You Should See

- Sphere divided into ~18 colored patches (LOD 0 clusters)
- Each patch is a different color
- Patches are roughly equal in size (~128 triangles each)
- Adjacent patches share edges cleanly (no gaps)

---

## Current Implementation Status

### Completed (Milestone 7 Phase 1)
- [x] Triangle adjacency graph construction
- [x] Greedy BFS partitioning
- [x] METIS integration (optional)
- [x] Cluster creation with bounds
- [x] QEM-based LOD hierarchy
- [x] Debug color visualization
- [x] Virtual Geometry debug panel

### Planned (Future Milestones)
- [ ] Binary .micluster cache format
- [ ] Indirect draw rendering
- [ ] GPU frustum culling compute shader
- [ ] Screen-space error LOD selection
- [ ] Visibility buffer pipeline
- [ ] Hi-Z occlusion culling
- [ ] Streaming and memory management

---

## Performance Characteristics

| Mesh Size | Clusters (LOD 0) | LOD Levels | Clustering Time |
|-----------|------------------|------------|-----------------|
| 2K tris   | ~18              | 3          | ~1-2 ms         |
| 10K tris  | ~80              | 4          | ~5-10 ms        |
| 100K tris | ~800             | 6          | ~50-100 ms      |
| 1M tris   | ~8000            | 8          | ~500-1000 ms    |

*Times measured on single CPU thread. Production implementations use multi-threading.*

---

## References

1. **Nanite (UE5)** - [A Deep Dive into Nanite Virtualized Geometry](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
2. **METIS** - [Graph Partitioning Library](http://glaros.dtc.umn.edu/gkhome/metis/metis/overview)
3. **QEM** - [Surface Simplification Using Quadric Error Metrics](https://www.cs.cmu.edu/~./garland/Papers/quadrics.pdf)
4. **Multiresolution Meshes** - [Ponchio's PhD Thesis](http://vcg.isti.cnr.it/~ponchio/download/ponchio_phd.pdf)
