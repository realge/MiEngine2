# Milestones 7-13: Virtual Geometry System

This document covers all Virtual Geometry milestones from initial clustering to Hi-Z occlusion culling.

## Milestone Status Overview
| Phase | Description | Status |
|-------|-------------|--------|
| M7 | Clustering + DAG | **COMPLETE** |
| M8 | Binary .micluster cache | **COMPLETE** |
| M9 | Indirect draw infrastructure | **COMPLETE** |
| M10 | GPU frustum culling | **COMPLETE** |
| M11 | LOD selection compute shader | **COMPLETE** |
| M12 | Graphics pipeline + rendering | **COMPLETE** |
| M12.5 | GPU-driven rendering | **COMPLETE** |
| M13 | Hi-Z occlusion culling | **COMPLETE** |
| M14 | Streaming + memory management | Planned |
| M15 | DAG traversal for per-cluster LOD | Planned |
| M16 | Software rasterization | Planned |

---

## M7: Clustering + DAG (2025-12-07)

**Goal:** Cluster-based mesh partitioning and LOD hierarchy (foundation for virtualized geometry rendering).

### Pre-VirtualGeo Foundation Fixes
```
VulkanRenderer.cpp
├── Expanded descriptor pool from ~220 to ~720 sets
├── Added VK_DESCRIPTOR_TYPE_STORAGE_BUFFER support (20 per frame)
└── Added VK_DESCRIPTOR_TYPE_STORAGE_IMAGE support (8 per frame)

include/culling/
├── FrustumCulling.h        - CPU & GPU frustum culling system
└── FrustumCulling.cpp      - Frustum plane extraction, sphere/AABB tests

shaders/culling/
└── frustum_cull.comp       - GPU compute shader for cluster culling
```

### New Virtual Geometry Files
```
include/virtualgeo/
├── VirtualGeoTypes.h       - Core types (Cluster, ClusterGroup, ClusteredMesh, GPU structs)
├── MeshClusterer.h         - Mesh-to-cluster conversion API
├── GraphPartitioner.h      - Built-in multilevel k-way graph partitioner (METIS-like)
└── ClusterDAGBuilder.h     - LOD hierarchy builder API

src/virtualgeo/
├── MeshClusterer.cpp       - Graph partitioning into ~128 tri clusters
├── GraphPartitioner.cpp    - Multilevel k-way partitioning algorithm (no external deps)
└── ClusterDAGBuilder.cpp   - QEM edge collapse, multi-level DAG construction

include/debug/
└── VirtualGeoDebugPanel.h  - ImGui debug panel for cluster visualization

src/debug/
└── VirtualGeoDebugPanel.cpp - Cluster info, LOD selector, visualization options

src/Games/VirtualGeoTest/
└── VirtualGeoTestGame.h    - Test game mode (Mode 11) for clustering demonstration
```

### Key Data Structures
```cpp
// ~128 triangle cluster (fundamental rendering unit)
struct Cluster {
    uint32_t clusterId, lodLevel, meshId;
    uint32_t vertexOffset, vertexCount;
    uint32_t indexOffset, triangleCount;
    glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;
    glm::vec3 aabbMin, aabbMax;
    float lodError, parentError;
    uint32_t parentClusterStart, parentClusterCount;
    uint32_t childClusterStart, childClusterCount;
    uint32_t materialIndex, flags;
    glm::vec4 debugColor;
};

// Complete Virtual Geo-ready mesh
struct ClusteredMesh {
    std::vector<Cluster> clusters;
    std::vector<ClusterVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t maxLodLevel;
    // Root/leaf cluster indices for traversal
};
```

### GraphPartitioner Algorithm (METIS-like)
The built-in partitioner uses the multilevel paradigm:
1. **Coarsening Phase**: Contract graph using Heavy Edge Matching
2. **Initial Partitioning**: Greedy Graph Growing on coarsest level
3. **Uncoarsening + Refinement**: KL/FM-style refinement
4. **Post-processing**: Merge small partitions into neighbors

### Usage
```cpp
#include "include/virtualgeo/MeshClusterer.h"
#include "include/virtualgeo/ClusterDAGBuilder.h"

MiEngine::MeshClusterer clusterer;
MiEngine::ClusteringOptions options;
options.targetClusterSize = 128;
options.maxLodLevels = 8;
options.verbose = true;

MiEngine::ClusteredMesh clusteredMesh;
clusterer.clusterMesh(vertices, indices, options, clusteredMesh);

MiEngine::ClusterDAGBuilder dagBuilder;
dagBuilder.buildDAG(clusteredMesh, options);
```

---

## M8: Binary .micluster Cache (2025-12-09)

**Goal:** Binary serialization for clustered meshes with Asset Browser integration.

### New Files
```
include/virtualgeo/
└── ClusteredMeshCache.h     - Binary cache format definitions and API

src/virtualgeo/
└── ClusteredMeshCache.cpp   - Save/load implementation
```

### Binary Cache Format (.micluster)
```cpp
struct ClusteredMeshCacheHeader {
    char magic[8];              // "MICLUST1"
    uint32_t version;
    uint32_t flags;
    uint64_t sourceFileHash;
    uint64_t sourceModTime;
    uint32_t clusterCount;
    uint32_t groupCount;
    uint32_t maxLodLevel;
    uint32_t vertexCount;
    uint32_t indexCount;
};
```

### Asset Browser Features
- Right-click context menu: "Generate Clustered Mesh..." / "View Clustered Mesh Info..."
- Modal popup with options: cluster size (32-256), max LOD levels (1-16), debug colors
- Status column shows "Clustered" (blue) for assets with .micluster cache

---

## M9: Indirect Draw Infrastructure (2025-12-09)

**Goal:** GPU buffer structures and rendering infrastructure for virtual geometry.

### New Files
```
include/virtualgeo/
└── VirtualGeoRenderer.h     - GPU renderer class and buffer structures

src/virtualgeo/
└── VirtualGeoRenderer.cpp   - Buffer management, mesh upload, draw calls

shaders/virtualgeo/
├── cluster_cull.comp        - GPU frustum culling + LOD selection compute shader
├── cluster.vert             - Vertex shader for clustered geometry
└── cluster.frag             - Fragment shader with debug visualization modes
```

### GPU Buffer Structures
```cpp
struct GPUDrawCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
    uint32_t clusterOffset;
    uint32_t clusterCount;
    uint32_t pad0, pad1;
};

struct GPUCullingUniforms {
    glm::mat4 viewProjection;
    glm::mat4 view;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::vec4 screenParams;
    float lodBias;
    float errorThreshold;
    uint32_t totalClusters;
    uint32_t frameIndex;
};
```

### Limits
- MAX_CLUSTERS: 1,000,000
- MAX_INSTANCES: 10,000
- MAX_DRAWS: 100,000

---

## M10: GPU Frustum Culling (2025-12-09)

**Goal:** Compute pipeline for GPU-driven cluster culling with debug panel integration.

### VirtualGeoRenderer Extensions
```cpp
void createDescriptorSets();
void createCullingPipeline();
void setFrustumCullingEnabled(bool enabled);
void setLodSelectionEnabled(bool enabled);
void setLodBias(float bias);
void setErrorThreshold(float threshold);
void setDebugMode(uint32_t mode);
```

### Debug Panel Controls
- Statistics: mesh count, instance count, cluster count, visible clusters, draw calls
- Cull rate percentage display
- Frustum culling toggle
- LOD selection toggle
- LOD bias slider (0.1 - 4.0)
- Error threshold slider (0.1 - 10.0 pixels)
- Debug mode dropdown (Normal, Cluster Colors, Normals, LOD Levels)

---

## M11-12: LOD Selection & Graphics Pipeline (2025-12-09)

**Goal:** Complete graphics pipeline for VirtualGeo rendering with LOD selection.

### ClusterVertex Format (48 bytes)
```cpp
struct ClusterVertex {
    glm::vec3 position;  // offset 0
    float pad0;          // offset 12
    glm::vec3 normal;    // offset 16
    float pad1;          // offset 28
    glm::vec2 texCoord;  // offset 32
    glm::vec2 pad2;      // offset 40
};
```

### VGPushConstants (per-instance, 80 bytes)
```cpp
struct VGPushConstants {
    glm::mat4 model;           // 64 bytes - per-instance transform
    uint32_t debugMode;        // 4 bytes
    uint32_t lodLevel;         // 4 bytes
    uint32_t clusterId;        // 4 bytes
    uint32_t pad;              // 4 bytes
};
```

### Bug Fixes Applied (2025-12-10)
1. Per-instance transform issue: Moved model matrix from UBO to push constants
2. Y-flip issue: Applied `projection[1][1] *= -1.0f` for Vulkan coordinate system
3. Winding order: Changed to `VK_FRONT_FACE_CLOCKWISE` to match PBR pipeline
4. Index conversion: Local cluster indices converted to global during upload
5. All-LOD rendering: Fixed by implementing per-LOD index ranges

---

## M12.5: GPU-Driven Rendering (2025-12-10)

**Goal:** GPU-driven rendering with compute culling, per-frame buffers, and indirect draw.

### New Structures
```cpp
struct PerFrameResources {
    VkBuffer indirectBuffer;
    VkBuffer drawCountBuffer;
    VkBuffer visibleClusterBuffer;
    VkDescriptorSet cullingDescSet;
};

struct MergedMeshData {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    VkBuffer clusterBuffer;
    uint32_t totalVertices, totalIndices, totalClusters;
    bool dirty;
};

struct GPUClusterDataExt {
    glm::vec4 boundingSphere;
    glm::vec4 aabbMin;               // w = lodError
    glm::vec4 aabbMax;               // w = parentError
    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t globalIndexOffset;
    uint32_t triangleCount;
    uint32_t lodLevel;
    uint32_t materialIndex;
    uint32_t flags;
    uint32_t instanceId;
};
```

### Rendering Modes
| Mode | Description | Draw Method |
|------|-------------|-------------|
| Direct (default) | Manual LOD selection per instance | `vkCmdDrawIndexed` per instance |
| GPU-Driven | Compute culling + indirect draw | `vkCmdDrawIndexedIndirectCount` |

### LOD Selection Modes
| Mode | Description |
|------|-------------|
| Forced LOD | Manual slider selection - all clusters at selected LOD level |
| Auto LOD | Distance-based - instance center determines LOD for all clusters |

---

## M13: Hi-Z Occlusion Culling (2025-12-10, Updated 2025-12-13)

**Goal:** Hierarchical-Z occlusion culling for hidden cluster rejection using temporal reprojection.

### New Files
```
shaders/virtualgeo/
├── hiz_generate.comp       - Hi-Z pyramid generation (mip 1+ via max reduction)
├── hiz_copy.vert           - Fullscreen triangle vertex shader
├── hiz_copy.frag           - Depth buffer to Hi-Z mip 0 copy
├── hiz_debug.vert          - Hi-Z visualization vertex shader
└── hiz_debug.frag          - Hi-Z visualization fragment shader
```

### Hi-Z Resources
```cpp
// Hi-Z pyramid
VkImage m_hizImage;                    // R32_SFLOAT with mip chain
VkImageView m_hizImageView;            // Full mip view for sampling
std::vector<VkImageView> m_hizMipViews;// Per-mip views for compute writes
VkSampler m_hizSampler;                // Nearest filtering sampler

// Hi-Z generation
VkPipeline m_hizPipeline;              // Compute pipeline for mip 1+ generation

// Hi-Z copy pass (graphics pass for mip 0)
VkRenderPass m_hizCopyRenderPass;
VkFramebuffer m_hizCopyFramebuffer;
VkPipeline m_hizCopyPipeline;
VkPipelineLayout m_hizCopyPipelineLayout;
VkDescriptorSetLayout m_hizCopyDescSetLayout;
VkDescriptorSet m_hizCopyDescSet;

// Hi-Z debug visualization
VkPipeline m_hizDebugPipeline;
VkPipelineLayout m_hizDebugPipelineLayout;
VkDescriptorSet m_hizDebugDescSet;
bool m_hizDebugEnabled;
float m_hizDebugMipLevel;
uint32_t m_hizDebugMode;
```

### Temporal Occlusion Culling Pipeline
Uses **previous frame's depth** to cull current frame's clusters:

```
Frame N:
1. buildHiZPyramid() - Sample Frame N-1's depth buffer
   ├── Graphics pass: Copy depth → Hi-Z mip 0
   └── Compute pass: Generate mips 1+ via max reduction
2. dispatchCulling() - Test clusters against Hi-Z
3. Render pass - Clear depth, render visible clusters, STORE depth
4. Depth available for Frame N+1
```

### Hi-Z Pyramid Generation
- **Mip 0**: Graphics pass samples depth buffer (avoids compute shader depth sampling issues)
- **Mip 1+**: Compute shader with 2x2 MAX reduction (conservative - furthest depth wins)

### Occlusion Test Algorithm (cluster_cull.comp)
```glsl
bool occlusionCullSphere(vec3 worldCenter, float worldRadius) {
    // 1. Project sphere center to screen UV
    vec4 clipCenter = viewProjection * vec4(worldCenter, 1.0);
    vec2 screenUV = (clipCenter.xy / clipCenter.w) * 0.5 + 0.5;

    // 2. Calculate mip level based on screen-space size (+2 for coarser sampling)
    float pixelDiameter = worldRadius * screenHeight / clipCenter.w;
    float mipLevel = clamp(log2(pixelDiameter) + 2.0, 0.0, maxMipLevel);

    // 3. Sample Hi-Z at center (single sample - mip already covers area)
    float hizDepth = textureLod(hizPyramid, screenUV, mipLevel).r;

    // 4. Skip if no occluder (sky/far plane)
    if (hizDepth > depthThreshold) return false;

    // 5. Project sphere FRONT with 1.5x margin (prevents self-occlusion)
    vec3 sphereFront = worldCenter - viewDir * worldRadius * 1.5;
    float sphereNearZ = project(sphereFront).z;

    // 6. Cull if sphere front is behind Hi-Z depth
    return sphereNearZ > (hizDepth + depthBias);
}
```

### Key Implementation Details

**Depth Buffer Preservation:**
```cpp
// VulkanRenderer.cpp - render pass depth attachment
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Preserve for next frame
depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
```

**First-Frame Handling:**
```cpp
// Skip Hi-Z on first frame (no valid previous depth)
if (m_firstFrame) {
    m_firstFrame = false;
    return;
}
// Also disable occlusion in culling uniforms on first frame
m_cullingUniforms.enableOcclusionCulling = (m_occlusionCullingEnabled && !m_firstFrame) ? 1 : 0;
```

**Self-Occlusion Prevention:**
- 1.5x radius margin when projecting sphere front
- Adjustable depth bias (default 0.001)
- Single center sample avoids edge cases hitting sky

### Adjustable Parameters (Debug Panel)
| Parameter | Default | Description |
|-----------|---------|-------------|
| `hizMaxMipLevel` | 3.0 | Max mip to sample (higher = faster, less accurate) |
| `hizDepthBias` | 0.001 | Bias added to comparison (higher = more conservative) |
| `hizDepthThreshold` | 0.999 | Depth considered "no occluder" |

### Debug Visualization Modes
- **Grayscale**: White=far(1.0), Black=near(0.0)
- **Threshold**: Color-coded depth ranges
- **UV Test**: Gradient pattern (ignore Hi-Z)

### API
```cpp
// Enable/disable
renderer->setOcclusionCullingEnabled(true);

// Adjust parameters
renderer->setHiZDepthBias(0.001f);
renderer->setHiZMaxMipLevel(3.0f);
renderer->setHiZDepthThreshold(0.999f);

// Debug visualization
renderer->setHiZDebugEnabled(true);
renderer->setHiZDebugMipLevel(0.0f);
renderer->setHiZDebugMode(0);  // 0=grayscale, 1=threshold, 2=UV
```

### Performance Notes
- Hi-Z pyramid generation: ~0.1ms (compute shader)
- Occlusion test: Negligible (part of existing culling dispatch)
- Memory: R32_SFLOAT mip chain (~1.33x base resolution)

### Known Limitations
- **Temporal lag**: 1-frame delay can cause pop-in with fast camera movement
- **Depth precision**: Standard depth buffer loses precision at distance (consider reversed-Z)
- **Single-phase**: No two-phase retest for newly visible objects
- **Per-cluster**: Self-occlusion possible within same object (mitigated by 1.5x margin)

### Future Optimization: Remove Hi-Z Copy Pipeline
Currently we create a **full graphics pipeline** (render pass, framebuffer, shaders) just to copy depth buffer to Hi-Z mip 0. This was necessary because compute shader depth sampling wasn't working initially.

Now that depth buffer preservation is fixed (`storeOp = STORE`), the compute shader approach may work:
```cpp
// Potential simplification - use compute for mip 0 too
// Would eliminate: hiz_copy.vert, hiz_copy.frag, m_hizCopyRenderPass,
//                  m_hizCopyFramebuffer, m_hizCopyPipeline, etc.
```
**Status**: Graphics pipeline works reliably. Compute optimization deferred.

---

## Future Milestones (Planned)

### M14: PBR Materials for Virtual Geometry
- Add texture bindings to cluster pipeline
- Per-cluster material index (already in ClusterDataExt)
- Bindless textures or texture arrays
- Upgrade cluster.frag to full PBR shading

### M15: Forward+ Lighting
- Tile-based light culling compute pass
- Reuse Hi-Z for depth bounds per tile
- Per-tile light index lists
- Efficient many-light support

### M16: Streaming + Memory Management
- Virtual memory allocation
- Page-based cluster streaming
- Memory budget management

### M17: DAG Traversal for Per-Cluster LOD
- GPU-based parent-child traversal
- Per-cluster LOD selection (Nanite-style)
- Seamless LOD transitions

### M18: Software Rasterization
- Compute shader rasterization for small triangles
- Hybrid HW/SW rasterization pipeline
- Visibility buffer approach

### M19: Two-Phase Occlusion Culling
- Phase 1: Cull with previous frame's Hi-Z
- Render visible clusters
- Phase 2: Retest culled clusters with updated Hi-Z
- Reduces temporal pop-in artifacts
