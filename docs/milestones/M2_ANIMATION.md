# Milestone 2: Skeletal Animation System (2025-11-30)

**Goal:** GPU-based skeletal animation with FBX skeleton/animation extraction.

## New Files
```
include/animation/
├── Skeleton.h              - Bone hierarchy, inverse bind poses
├── AnimationClip.h         - Keyframe data, sampling
└── SkeletalMeshComponent.h - Per-instance animation state

src/animation/
├── Skeleton.cpp            - Global pose computation
├── AnimationClip.cpp       - Keyframe interpolation (position, rotation, scale)
└── SkeletalMeshComponent.cpp - Animation playback, bone matrix update

include/Utils/
└── SkeletalVertex.h        - Extended vertex with bone indices/weights

include/mesh/
└── SkeletalMesh.h          - GPU mesh for skeletal vertices

src/mesh/
└── SkeletalMesh.cpp        - Buffer creation for SkeletalVertex

src/loader/
└── SkeletalModelLoader.cpp - FBX skeleton/skinning/animation extraction

src/scene/
└── SceneSkeletal.cpp       - Scene integration for skeletal models

shaders/
└── skeletal.vert           - GPU skinning vertex shader
```

## Modified Files
- `include/loader/ModelLoader.h` - Added SkeletalMeshData, SkeletalModelData structs
- `include/mesh/Mesh.h` - Added virtual methods for derived SkeletalMesh
- `include/scene/Scene.h` - Added skeletal mesh component, loadSkeletalModel()

## Architecture
- **Skeleton**: Stores bones in flat array, computes global poses from local poses
- **AnimationClip**: Separate position/rotation/scale tracks per bone
- **SkeletalMeshComponent**: Per-instance playback state, caches final bone matrices
- **SkeletalVertex**: 92 bytes (60 base + 16 boneIndices + 16 boneWeights)
- **GPU Skinning**: 4 bone influences per vertex, weighted matrix blending

## Usage
```cpp
// Load skeletal model with animations
m_Scene->loadSkeletalModel("character.fbx", transform);

// Play animation on a skeletal mesh instance
m_Scene->playAnimation(instanceIndex, animationIndex, loop);

// Or direct control
auto* instance = m_Scene->getMeshInstance(0);
if (instance->skeletalMesh) {
    instance->skeletalMesh->playAnimation(clip, true);
    instance->skeletalMesh->setPlaybackSpeed(1.5f);
}
```

## Shader Integration
- `skeletal.vert` extends `pbr.vert` with bone matrix skinning
- Bone matrices passed via UBO (set 1, binding 0)
- Max 256 bones supported
- Compiled to `skeletal.vert.spv`

## Rendering Integration
- Skeletal rendering pipeline in VulkanRenderer (createSkeletalPipeline)
- Bone matrix descriptor set layout (set 1) for per-instance bone data
- Per-instance bone matrix UBO management (createSkeletalInstanceResources)
- Scene::draw() detects skeletal instances and uses skeletal pipeline
- Scene::update() updates skeletal animation state each frame

## TODO (Future Enhancements)
- Animation blending between clips
- Debug bone visualization
- Ragdoll physics integration
