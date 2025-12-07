# Milestone 6: Hardware Ray Tracing System

**Goal:** Real-time ray-traced reflections and soft shadows using VK_KHR_ray_tracing_pipeline with hybrid raster/RT rendering and optional toggle.

**Date:** 2025-12-03

---

## Overview

This milestone adds hardware ray tracing support to MiEngine2, enabling:
- Real-time ray-traced reflections on metallic/glossy surfaces
- Soft shadows from directional lights
- Hybrid rendering (rasterization for primary visibility, RT for secondary effects)
- Optional toggle between raster-only and hybrid modes
- Temporal and spatial denoising for low SPP rendering

---

## New Files

```
include/raytracing/
├── RayTracingSystem.h              - Main RT system (BLAS/TLAS, pipeline, rendering)
├── RayTracingTypes.h               - Structs: RTSettings, BLASInfo, TLASInfo, RTMaterialData
├── AccelerationStructure.h         - BLAS/TLAS management classes (Phase 2)
└── Denoiser.h                      - Temporal/spatial denoising (Phase 5)

src/raytracing/
├── RayTracingSystem.cpp            - Core implementation
├── AccelerationStructure.cpp       - AS build/update logic (Phase 2)
└── Denoiser.cpp                    - Denoising compute shaders (Phase 5)

shaders/raytracing/
├── raygen.rgen                     - Ray generation (reflections + shadow rays)
├── closesthit.rchit                - Closest hit (material eval, env sampling)
├── miss.rmiss                      - Miss shader (IBL environment sampling)
├── miss_shadow.rmiss               - Shadow miss (point is lit)
├── rt_common.glsl                  - Shared utilities (RNG, sampling)
├── denoise_temporal.comp           - Temporal accumulation
└── denoise_spatial.comp            - Spatial bilateral filter

include/debug/
└── RayTracingDebugPanel.h          - ImGui RT controls

src/debug/
└── RayTracingDebugPanel.cpp
```

---

## Modified Files

### VulkanRenderer.cpp

1. **Device Extensions** (line 167-183):
   - Added ray tracing extension list
   - Conditional extension enabling based on hardware support

2. **API Version** (line 538):
   - Upgraded from VK_API_VERSION_1_0 to VK_API_VERSION_1_2

3. **checkDeviceExtensionSupport** (line 231-269):
   - Added RT extension detection
   - Sets global `g_RayTracingSupported` flag

4. **createLogicalDevice** (line 630-703):
   - Added VkPhysicalDeviceFeatures2 with pNext chain for RT features
   - Enables: bufferDeviceAddress, accelerationStructure, rayTracingPipeline

5. **initVulkan** (line 465-466):
   - Added call to initRayTracing()

6. **initRayTracing** (line 469-495):
   - New function to initialize RayTracingSystem

### VulkanRenderer.h

1. Added include for RayTracingSystem.h
2. Added members:
   - `bool m_RayTracingSupported`
   - `std::unique_ptr<MiEngine::RayTracingSystem> rayTracingSystem`
3. Added accessors:
   - `isRayTracingSupported()`
   - `getRayTracingSystem()`
   - `initRayTracing()`

---

## Implementation Phases

### Phase 1: Vulkan RT Foundation (COMPLETED)
- [x] Upgrade Vulkan API to 1.2
- [x] Add RT extension strings (conditional on hardware support)
- [x] Query RT device support with graceful fallback
- [x] Enable RT features in device creation (pNext chain)
- [x] Create RayTracingSystem skeleton with extension function loading
- [x] Query RT properties (SBT alignment, max recursion depth)

### Phase 2: Acceleration Structures (COMPLETED)
- [x] Implement BLASInfo struct and BLAS creation per mesh
- [x] Create vertex/index buffer with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
- [x] Implement TLASInfo and TLAS build from scene instances
- [x] Add scratch buffer pool management
- [x] Hook Scene::getMeshInstances and MiWorld::getActors for AS updates
- [x] Implement TLAS refit for dynamic objects (updateTLAS)

### Phase 3: RT Pipeline and Shaders (TODO)
- [ ] Write raygen.rgen (G-buffer sampling, reflection/shadow rays)
- [ ] Write closesthit.rchit (material eval, environment reflection)
- [ ] Write miss.rmiss (IBL environment map sampling)
- [ ] Write miss_shadow.rmiss (shadow ray miss = lit)
- [ ] Create Shader Binding Table (SBT) with proper alignment
- [ ] Create RT pipeline and pipeline layout
- [ ] Create reflection output image (RGBA16F) and shadow output (R16F)

### Phase 4: Hybrid Integration (TODO)
- [ ] Add G-buffer outputs to PBR pass (normal, metallic-roughness)
- [ ] Create RT descriptor sets (TLAS, outputs, G-buffer, uniforms)
- [ ] Integrate RT pass into drawFrame() after shadow/water passes
- [ ] Modify pbr.frag to sample and blend RT outputs
- [ ] Add push constant flags for RT enable/disable
- [ ] Implement render mode toggle (raster only vs hybrid)

### Phase 5: Denoising (TODO)
- [ ] Implement temporal accumulation compute shader
- [ ] Add history buffer and reprojection logic
- [ ] Implement spatial bilateral filter (edge-aware)
- [ ] Chain temporal -> spatial in denoise pass
- [ ] Handle disocclusion detection

### Phase 6: Debug UI and Polish (TODO)
- [ ] Create RayTracingDebugPanel (enable, SPP, debug modes)
- [ ] Add performance profiling (GPU timestamps)
- [ ] BLAS compaction for memory optimization
- [ ] Test on various scenes and validate quality

---

## Usage

### Checking RT Support
```cpp
if (renderer->isRayTracingSupported()) {
    auto* rtSystem = renderer->getRayTracingSystem();
    rtSystem->getSettings().enabled = true;
}
```

### RT Settings
```cpp
RTSettings& settings = rtSystem->getSettings();
settings.enabled = true;
settings.samplesPerPixel = 2;
settings.maxBounces = 2;
settings.enableReflections = true;
settings.enableSoftShadows = true;
settings.shadowSoftness = 0.02f;
settings.enableDenoising = true;
```

---

## Performance Targets

| Metric | Target |
|--------|--------|
| SPP | 1-4 |
| Max bounces | 1-2 |
| Frame time (RT pass) | < 8ms |
| Resolution | Full or 1/2 with upscale |
| FPS | 30-60 |

---

## Memory Budget (Estimated)

| Component | Size |
|-----------|------|
| BLAS (50 unique meshes) | ~50 MB |
| TLAS (1000 instances) | ~2 MB |
| SBT | ~0.5 MB |
| Output images (1080p) | ~16 MB |
| Denoiser history | ~8 MB |
| **Total** | **~77 MB** |

---

## Hardware Requirements

- GPU: NVIDIA RTX 20-series or newer / AMD RDNA2 or newer
- Vulkan 1.2+ support
- Ray tracing extensions:
  - VK_KHR_acceleration_structure
  - VK_KHR_ray_tracing_pipeline
  - VK_KHR_buffer_device_address
  - VK_KHR_deferred_host_operations
  - VK_KHR_spirv_1_4
  - VK_KHR_shader_float_controls

---

## Fallback Behavior

If RT hardware not detected:
1. `RayTracingSystem::isSupported()` returns false
2. RT menu options grayed out in debug panel
3. Renderer continues using existing shadow maps and IBL reflections
4. No crash or error - graceful degradation

---

## Future Extensibility

This architecture supports easy extension to:
- **Global Illumination**: Increase max bounces, spawn indirect rays
- **Area Lights**: Add light geometry sampling
- **Caustics**: Photon mapping pass
- **Transparency/Refraction**: Any-hit shader
- **Volumetric Rendering**: Ray marching in miss shader
- **ReSTIR**: Reservoir-based importance sampling
