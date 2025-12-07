# Denoiser Fix Progress - Session Summary

## Current Work
Fixing the ray tracing denoiser system for MiEngine2's Milestone 6 (Hardware Ray Tracing).

## Problem Description
User reported two main issues:
1. **High noise with high metallic + high roughness materials** - especially visible when looking at bright areas like windows in the skybox
2. **Spatial filter has no visible effect** - toggling spatial denoising on/off makes no difference

## Root Cause Found (Spatial Filter Bug)
The spatial denoiser uniform buffer was incorrectly sized. The shader expects the **same layout as temporal** (with matrices at the beginning), but:

- Buffer was created with `sizeof(SpatialDenoiseUniforms)` = 16 bytes
- Descriptor range was set to 16 bytes
- But shader expects full `TemporalDenoiseUniforms` layout = 148 bytes

The spatial shader reinterprets the last 4 fields of the TemporalDenoiseUniforms struct:
- `temporalBlend` → `sigmaColor`
- `varianceClipGamma` → `sigmaSpatial`
- `frameNumber` → `kernelRadius`
- `enableTemporal` → `enabled`

## Fixes Applied

### 1. Fixed Spatial Uniform Buffer Size (RayTracingSystem.cpp line 2507-2509)
```cpp
// BEFORE:
VkDeviceSize spatialBufferSize = sizeof(SpatialDenoiseUniforms);

// AFTER:
VkDeviceSize spatialBufferSize = sizeof(TemporalDenoiseUniforms);
```

### 2. Fixed Spatial Descriptor Range (RayTracingSystem.cpp line 2799-2803)
```cpp
// BEFORE:
spatialUniformInfo.range = sizeof(SpatialDenoiseUniforms);

// AFTER:
spatialUniformInfo.range = sizeof(TemporalDenoiseUniforms);
```

## Previous Fixes (Already Applied)

### Temporal Shader Variance Logic (denoise_temporal.comp)
Fixed the disocclusion detection - was incorrectly using MORE current frame for high variance areas:
```glsl
// Only increase blend for actual disocclusion (history differs AND high variance)
if (historyDiff > 1.0 && variance > 0.3) {
    blendFactor = min(blendFactor * 4.0, 0.5);
}
```

### Spatial Shader Read-Write Hazard (denoise_spatial.comp)
Fixed by pre-loading all neighbor values into local arrays before processing:
```glsl
vec3 neighborRefl[49];  // Max 7x7 kernel
float neighborShadow[49];
// First pass: load all neighbors
// Second pass: compute weighted bilateral filter using pre-loaded values
```

### Firefly Clamping (raygen.rgen)
Added HDR clamping to reduce noise from bright spots:
```glsl
vec3 clampedColor = min(payload.color, vec3(3.0));
```

### Cone Angle Capping (raygen.rgen)
Reduced roughness-based jitter for glossy reflections:
```glsl
float maxConeAngle = 0.17;  // ~10 degrees max
float coneAngle = min(alpha * 0.5, maxConeAngle);
```

## Default Denoiser Settings (RayTracingTypes.h)
```cpp
struct DenoiserSettings {
    bool enableTemporal = true;
    bool enableSpatial = true;
    float temporalBlend = 0.05f;    // 5% current, 95% history
    float varianceClipGamma = 2.0f;
    int spatialFilterRadius = 3;    // 7x7 kernel
    float spatialColorSigma = 0.3f;
    float spatialSigma = 3.0f;
};
```

## Files Modified
1. `src/raytracing/RayTracingSystem.cpp` - Fixed buffer sizes and descriptor ranges
2. `shaders/raytracing/denoise_temporal.comp` - Fixed variance logic
3. `shaders/raytracing/denoise_spatial.comp` - Fixed read-write hazard
4. `shaders/raytracing/raygen.rgen` - Added firefly clamping, cone angle capping
5. `include/raytracing/RayTracingTypes.h` - Adjusted default settings

## Testing Needed
1. Rebuild the project (clear PDB files first if build errors)
2. Run MiEngine2.exe
3. Test with high metallic + high roughness cube
4. Toggle spatial denoiser on/off - should now show visible difference
5. Verify noise reduction is effective

## Session 2 Fixes (2025-12-05)

### Issue 1: Spatial Filter 50% FPS Drop
**Root Cause:** Default `spatialFilterRadius = 3` created 7x7 kernel (49 samples × 2 images = 98 texture reads per pixel).

**Fix:** Changed defaults in `RayTracingTypes.h`:
```cpp
// BEFORE:
int spatialFilterRadius = 3;    // 7x7 kernel = 49 samples
float spatialColorSigma = 0.3f;
float spatialSigma = 3.0f;

// AFTER:
int spatialFilterRadius = 1;    // 3x3 kernel = 9 samples (5x faster)
float spatialColorSigma = 0.5f; // Increased for more blur with fewer samples
float spatialSigma = 1.5f;      // Reduced to match smaller kernel
```

### Issue 2: Temporal Toggle Does Nothing
**Root Cause:** Variance clipping was too aggressive. The `clipToNeighborhood` function used the midpoint of min/max, causing history to be heavily clamped toward current frame (defeating temporal accumulation).

**Fix:** Rewrote `clipToNeighborhood` in `denoise_temporal.comp`:
```glsl
// BEFORE: Complex scaling that collapsed to current frame
vec3 center = (minColor + maxColor) * 0.5;
vec3 extents = (maxColor - minColor) * 0.5 * uniforms.varianceClipGamma;
// ... complex offset scaling

// AFTER: Simple AABB clipping centered on average
vec3 center = avgColor;  // Use average for better centering
vec3 halfExtent = (maxColor - minColor) * 0.5;
vec3 extents = halfExtent * uniforms.varianceClipGamma;
vec3 clipMin = center - extents;
vec3 clipMax = center + extents;
return clamp(color, clipMin, clipMax);
```

Also reduced `varianceClipGamma` from 2.0 to 1.5 to prevent over-expansion.

### Files Modified This Session
1. `include/raytracing/RayTracingTypes.h` - Adjusted default denoiser settings
2. `shaders/raytracing/denoise_temporal.comp` - Fixed variance clipping algorithm

### Shader Recompilation
Run: `glslc -fshader-stage=compute denoise_temporal.comp -o denoise_temporal.comp.spv`

## Session 3 Fixes (2025-12-05)

### Issues Reported:
1. **Blurry reflections** - Too much history weight
2. **Ghosting/afterimage when moving camera** - No history rejection on camera movement
3. **Noise still visible with temporal ON** - Variance clipping defeating accumulation

### Root Cause Analysis:
The previous variance clipping (min/max based) was flawed:
- Used min/max of 3x3 neighborhood which is too tight
- History almost always got clipped to current values
- Result: temporal accumulation had no effect

### Fixes Applied:

**1. Complete rewrite of temporal shader (`denoise_temporal.comp`):**

Changed from min/max clipping to **standard deviation based clipping**:
```glsl
// NEW: Compute mean and standard deviation
vec3 m1 = vec3(0.0);  // First moment (mean)
vec3 m2 = vec3(0.0);  // Second moment (variance)
for (3x3 neighborhood) {
    m1 += sample;
    m2 += sample * sample;
}
m1 /= 9.0;
m2 /= 9.0;
vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

// Clipping bounds: mean +/- gamma * sigma
vec3 minRefl = m1 - gamma * sigma;
vec3 maxRefl = m1 + gamma * sigma;
```

**2. Adaptive blend factor based on clip amount:**
```glsl
float clipAmount = length(historyRefl.rgb - clippedHistoryRefl);
if (clipAmount > 0.1) {
    // History differs significantly - blend more towards current
    blendFactor = mix(blendFactor, 0.5, smoothstep(0.1, 0.5, clipAmount));
}
```

**3. Updated default settings (`RayTracingTypes.h`):**
```cpp
// BEFORE:
float temporalBlend = 0.05f;
float varianceClipGamma = 1.5f;
bool enableSpatial = true;

// AFTER:
float temporalBlend = 0.1f;     // 10% current - less blur
float varianceClipGamma = 1.0f; // Standard deviation based (tighter)
bool enableSpatial = false;     // Spatial disabled by default
```

### Why This Works Better:

| Issue | Old Behavior | New Behavior |
|-------|-------------|--------------|
| Blurry | 5% current, 95% history | 10% current, adaptive up to 50% |
| Ghosting | No camera awareness | Clip detection increases blend |
| Still noisy | Clipping killed accumulation | Proper sigma-based clipping |

### Files Modified:
1. `shaders/raytracing/denoise_temporal.comp` - Complete rewrite
2. `include/raytracing/RayTracingTypes.h` - New defaults

### Testing:
1. Rebuild project
2. With temporal ON: Should see smooth reflections that converge over ~10 frames
3. Camera movement: Should see brief noise then quick convergence (no ghosting)
4. Temporal OFF: Should see raw noisy RT output

## Session 4 Fixes (2025-12-05)

### Issues Still Present:
1. **Afterimage when moving camera** - History not being rejected
2. **Noise still visible** - Temporal not accumulating properly
3. **Highlights getting brighter** - Possible accumulation drift

### Root Cause Analysis:
1. Previous shader was too complex with multiple failure modes
2. History buffer lacked proper synchronization barriers between frames
3. Variance clipping logic was defeating accumulation

### Fixes Applied:

**1. Complete rewrite of temporal shader - simplified EMA approach:**
```glsl
// Simple exponential moving average
vec3 result = mix(clampedHistory, current, alpha);

// Adaptive alpha based on rejection amount
float rejection = length(history - clampedHistory) / max(length(history), 0.001);
if (rejection > 0.1) {
    alpha = mix(alpha, 0.8, smoothstep(0.1, 0.5, rejection));
}
```

**2. Added history buffer synchronization barriers (RayTracingSystem.cpp):**
```cpp
// History images -> next frame's compute read
finalBarriers[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
finalBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
finalBarriers[2].image = m_HistoryReflectionImage;
// ... also for shadow history
```

**3. Updated default settings:**
```cpp
float temporalBlend = 0.15f;    // 15% current (was 0.1)
float varianceClipGamma = 2.0f; // Looser clipping (was 1.0)
```

### Key Changes in Shader:
- Simplified from 140+ lines to ~110 lines
- Cleaner min/max neighborhood calculation
- Proper center/extent based clipping
- Rejection-based adaptive alpha
- First frame handling: `frameNumber <= 1` instead of `== 0`

### Files Modified:
1. `shaders/raytracing/denoise_temporal.comp` - Complete rewrite (simpler)
2. `src/raytracing/RayTracingSystem.cpp` - Added 4-barrier sync (was 2)
3. `include/raytracing/RayTracingTypes.h` - Adjusted defaults

### Testing:
1. Rebuild C++ project
2. Static camera: noise should reduce over ~7 frames
3. Moving camera: brief noise then convergence, NO ghosting
4. Toggle temporal: OFF should show raw noisy output

## Remaining Tasks
1. Phase 6: Add performance profiling (GPU timestamps)
2. Consider adding motion vector support for better camera movement handling
