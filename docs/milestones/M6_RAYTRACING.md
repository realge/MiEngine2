# Milestone 6: Hardware Ray Tracing System (2025-12-03 to 2025-12-04)

**Goal:** Real-time ray-traced reflections and soft shadows using VK_KHR_ray_tracing_pipeline.

## New Files
```
include/raytracing/
├── RayTracingSystem.h      - Main RT system (BLAS/TLAS, pipeline, rendering)
├── RayTracingTypes.h       - Structs: RTSettings, BLASInfo, TLASInfo, RTMaterialData

src/raytracing/
└── RayTracingSystem.cpp    - Core implementation (~2700 lines)

shaders/raytracing/
├── raygen.rgen             - Ray generation (reflections + shadow rays)
├── closesthit.rchit        - Closest hit (material eval, geometric normals)
├── miss.rmiss              - Miss shader (IBL environment sampling)
├── miss_shadow.rmiss       - Shadow miss (point is lit)
├── rt_common.glsl          - Shared utilities (RNG, sampling, Fresnel)
├── denoise_temporal.comp   - Temporal accumulation
└── denoise_spatial.comp    - Spatial bilateral filter

include/debug/
└── RayTracingDebugPanel.h  - ImGui RT controls

src/debug/
└── RayTracingDebugPanel.cpp

src/Games/RayTracingTest/
└── RayTracingTestGame.h    - RT test scene with reflective materials
```

## Modified Files
- `VulkanRenderer.h/cpp` - RT extensions, features, hybrid integration
- `shaders/pbr.frag` - RT output sampling, fallback ambient lighting

## Features Implemented
- **Acceleration Structures**: BLAS per-mesh, TLAS per-frame rebuild
- **RT Pipeline**: raygen, closesthit, 2 miss shaders (reflection + shadow)
- **Shader Binding Table**: Proper alignment and region setup
- **Per-Instance Materials**: Material buffer (binding 5) with base color, metallic, roughness
- **Soft Shadows**: Cone sampling for penumbra, tMin-based bias to prevent self-shadowing
- **Denoising**: Temporal accumulation + spatial bilateral filter (optional)
- **Hybrid Rendering**: RT outputs blended with rasterized PBR
- **Debug Panel**: Enable/disable, SPP, bounces, bias controls, debug modes
- **Fallback Ambient**: Hemisphere lighting when IBL is disabled

## RT Settings
```cpp
struct RTSettings {
    bool enabled = false;
    int samplesPerPixel = 1;        // 1-4 for real-time
    int maxBounces = 2;
    float reflectionBias = 0.001f;
    float shadowBias = 0.05f;       // tMin for shadow rays
    bool enableReflections = true;
    bool enableSoftShadows = true;
    float shadowSoftness = 0.02f;   // Cone angle for soft shadows
    bool enableDenoising = true;
    int debugMode = 0;              // 0=off, 1=normals, 2=depth, etc.
};
```

## Shadow Ray Implementation
- Uses `tMin` parameter as bias instead of position offset
- More robust for curved surfaces (spheres)
- shadowBias slider range: 0.001 - 0.5

## PBR Integration
- Set 5: RT outputs (rtReflections, rtShadows)
- Push constants: useRTReflections, useRTShadows (separate flags)
- Fallback ambient when IBL disabled (hemisphere lighting)
- Debug layer 15: RT Shadow visualization

## Debug Modes (raygen.rgen)
| Mode | Description |
|------|-------------|
| 0 | Off (normal rendering) |
| 1 | Normals |
| 2 | Depth (hit distance) |
| 3 | Metallic |
| 4 | Roughness |
| 5 | Reflections only |
| 6 | Shadows only |

## Known Limitations
- Geometric normals computed via heuristic (sphere detection by distance from center)
- No vertex buffer access in closesthit - proper normals would require bindless vertex data
- Denoiser output selection based on settings (raw vs denoised)

## Bug Fixes Applied
1. Fixed NdotL check that inverted shadows (removed - PBR handles lighting attenuation)
2. Fixed denoised output not being used (descriptor set now selects based on settings)
3. Added fallback ambient lighting when IBL is off
4. Fixed shadow bias - changed from position offset to tMin parameter
5. Fixed debug mode mismatch between panel and shader
6. Added separate useRTReflections/useRTShadows push constant flags

## Usage
```cpp
// Enable RT in test game
MiEngine::RayTracingSystem* rtSystem = m_Renderer->getRayTracingSystem();
rtSystem->getSettings().enabled = true;
rtSystem->getSettings().enableReflections = true;
rtSystem->getSettings().enableSoftShadows = true;

// Adjust shadow bias if seeing self-shadowing artifacts
rtSystem->getSettings().shadowBias = 0.1f;
```

## TODO (Future Enhancements)
- Bindless vertex buffer access for proper interpolated normals
- Global illumination (multi-bounce indirect lighting)
- Area light sampling
- Performance profiling GPU timestamps
