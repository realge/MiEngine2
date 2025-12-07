# MiEngine2 Development Milestones

## Overview
This document tracks the development progress of MiEngine2, a Vulkan-based 3D rendering engine.

---

## Milestone 1: Physics Foundation
**Status:** Complete
**Date:** 2025-11-29

### Goal
Component-based physics system foundation allowing selective physics on MeshInstances.

### Files Created
```
include/physics/
├── Component.h           - Base component class with ComponentType enum
├── ColliderComponent.h   - AABB/Sphere collider shapes
├── RigidBodyComponent.h  - Rigid body dynamics (mass, velocity, forces)
└── PhysicsWorld.h        - Physics simulation manager

src/physics/
├── ColliderComponent.cpp - World-space bounds, layer filtering
├── RigidBodyComponent.cpp - Force/impulse application
└── PhysicsWorld.cpp      - Fixed timestep update loop
```

### Files Modified
- `include/scene/Scene.h` - Added component pointers to MeshInstance, PhysicsWorld member
- `src/scene/Scene.cpp` - Added `enablePhysics()`, physics update integration

### Features Implemented
- [x] Optional physics via `shared_ptr` components (nullptr = no physics overhead)
- [x] `Scene::enablePhysics(index, bodyType)` helper for easy setup
- [x] RigidBodyType: Dynamic, Kinematic, Static
- [x] Gravity application with per-object gravity scale
- [x] Force and impulse API
- [x] Position constraints (lock X/Y/Z axes)
- [x] Collision layer bitmask filtering (structure only)
- [x] Fixed timestep physics (60 Hz default)

### Usage Example
```cpp
// Enable physics on a mesh instance
m_Scene->enablePhysics(0, RigidBodyType::Dynamic);

// Access and customize
auto* obj = m_Scene->getMeshInstance(0);
obj->rigidBody->mass = 2.0f;
obj->rigidBody->addImpulse({0, 10, 0});
```

### TODO (Future Work)
- [ ] Collision detection (AABB-AABB, Sphere-Sphere, AABB-Sphere)
- [ ] Collision response (impulse-based resolution)
- [ ] Raycast and spatial queries
- [ ] Debug visualization panel

---

## Milestone 2: Skeletal Animation System
**Status:** Complete (Core + Rendering Integration)
**Date:** 2025-11-30

### Goal
GPU-based skeletal animation with FBX skeleton/animation extraction.

### Files Created
```
include/animation/
├── Skeleton.h              - Bone hierarchy, inverse bind poses
├── AnimationClip.h         - Keyframe data, sampling with interpolation
└── SkeletalMeshComponent.h - Per-instance animation state

src/animation/
├── Skeleton.cpp            - Global pose computation from local poses
├── AnimationClip.cpp       - Keyframe interpolation (lerp for pos/scale, slerp for rotation)
└── SkeletalMeshComponent.cpp - Animation playback, bone matrix caching

include/Utils/
└── SkeletalVertex.h        - Extended vertex with bone indices/weights (92 bytes)

include/mesh/
└── SkeletalMesh.h          - GPU mesh class for skeletal vertices

src/mesh/
└── SkeletalMesh.cpp        - Buffer creation for SkeletalVertex format

src/loader/
└── SkeletalModelLoader.cpp - FBX extraction: skeleton, skinning weights, animations

src/scene/
└── SceneSkeletal.cpp       - Scene integration (loadSkeletalModel, playAnimation)

shaders/
└── skeletal.vert           - GPU skinning vertex shader (4 bone influences)
```

### Files Modified
- `include/loader/ModelLoader.h` - Added SkeletalMeshData, SkeletalModelData structs
- `include/mesh/Mesh.h` - Added virtual methods, protected constructor for inheritance
- `src/mesh/Mesh.cpp` - Added protected constructor for derived classes
- `include/scene/Scene.h` - Added skeletalMesh component, isSkeletal flag, loadSkeletalModel()

### Architecture

#### Data Flow
```
FBX File
    ↓
ModelLoader::LoadSkeletalModel()
    ↓
SkeletalModelData
├── meshes: vector<SkeletalMeshData>  (vertices with bone weights)
├── skeleton: shared_ptr<Skeleton>     (bone hierarchy)
└── animations: vector<AnimationClip>  (keyframe data)
    ↓
Scene creates:
├── SkeletalMesh (GPU buffers)
└── SkeletalMeshComponent (animation state)
    ↓
Each frame:
├── SkeletalMeshComponent::update(deltaTime)
├── Sample animation → local poses
├── Compute global poses → final bone matrices
└── Upload to GPU UBO
    ↓
skeletal.vert applies skinning
```

#### Memory Layout
| Component | Size |
|-----------|------|
| Standard Vertex | 60 bytes |
| SkeletalVertex | 92 bytes (+32 for bones) |
| Bone Matrix UBO | 16384 bytes (256 × mat4) |

#### Bone Influence
- 4 bones per vertex maximum
- Weights normalized to sum to 1.0
- Smallest weights discarded if >4 influences

### Features Implemented
- [x] Skeleton class with bone hierarchy
- [x] Bone inverse bind pose storage
- [x] Global pose computation from local transforms
- [x] AnimationClip with separate position/rotation/scale tracks
- [x] Keyframe interpolation (linear for pos/scale, slerp for rotation)
- [x] Animation looping support
- [x] Playback speed control
- [x] SkeletalVertex with 4 bone influences
- [x] Weight normalization
- [x] FBX skeleton extraction from deformers
- [x] FBX skinning weight extraction
- [x] FBX animation stack/layer extraction
- [x] SkeletalMesh GPU buffer class
- [x] Scene::loadSkeletalModel() integration
- [x] Scene::playAnimation() API
- [x] GPU skinning vertex shader

### Usage Example
```cpp
// Load skeletal model with animations
m_Scene->loadSkeletalModel("character.fbx", transform);

// Play animation by index
m_Scene->playAnimation(instanceIndex, 0, true);  // loop = true

// Direct control
auto* instance = m_Scene->getMeshInstance(0);
if (instance->skeletalMesh) {
    instance->skeletalMesh->playAnimation(clip, true);
    instance->skeletalMesh->setPlaybackSpeed(1.5f);
    instance->skeletalMesh->pauseAnimation();
    instance->skeletalMesh->setCurrentTime(0.5f);
}

// Get bone matrices for custom use
const auto& matrices = instance->skeletalMesh->getFinalBoneMatrices();
```

### Rendering Integration (Complete)
- [x] Create VkPipeline for skeletal meshes in VulkanRenderer
- [x] Create VkDescriptorSetLayout for bone matrix UBO (set 1, binding 0)
- [x] Allocate per-instance bone matrix UBOs
- [x] Update Scene::draw() to detect skeletal instances
- [x] Bind skeletal pipeline and bone UBO when drawing skeletal meshes
- [x] Compile skeletal.vert to SPIR-V

### TODO (Future Enhancements)
- [ ] Animation blending (crossfade between clips)
- [ ] Animation layers (additive blending)
- [ ] Root motion extraction
- [ ] Animation events/notifications
- [ ] Debug bone visualization (line rendering)
- [ ] Ragdoll physics integration
- [ ] IK (Inverse Kinematics) support

---

## Future Milestones

### Milestone 3: Collision Detection (Planned)
- AABB vs AABB intersection
- Sphere vs Sphere intersection
- AABB vs Sphere intersection
- Broadphase spatial partitioning
- Collision callbacks

### Milestone 4: Collision Response (Planned)
- Impulse-based resolution
- Restitution (bounciness)
- Friction
- Contact manifolds

### Milestone 5: Animation Blending (Planned)
- Crossfade transitions
- Blend trees
- Animation state machine
- Layered animations

### Milestone 6: Debug Visualization (Planned)
- Physics collider wireframes
- Skeleton bone visualization
- Contact point rendering
- Performance profiling overlay

---

## File Structure Summary

```
MiEngine2/
├── include/
│   ├── animation/
│   │   ├── Skeleton.h
│   │   ├── AnimationClip.h
│   │   └── SkeletalMeshComponent.h
│   ├── physics/
│   │   ├── Component.h
│   │   ├── ColliderComponent.h
│   │   ├── RigidBodyComponent.h
│   │   └── PhysicsWorld.h
│   ├── mesh/
│   │   ├── Mesh.h
│   │   └── SkeletalMesh.h
│   ├── loader/
│   │   └── ModelLoader.h
│   ├── scene/
│   │   └── Scene.h
│   └── Utils/
│       ├── CommonVertex.h
│       └── SkeletalVertex.h
├── src/
│   ├── animation/
│   │   ├── Skeleton.cpp
│   │   ├── AnimationClip.cpp
│   │   └── SkeletalMeshComponent.cpp
│   ├── physics/
│   │   ├── ColliderComponent.cpp
│   │   ├── RigidBodyComponent.cpp
│   │   └── PhysicsWorld.cpp
│   ├── mesh/
│   │   ├── Mesh.cpp
│   │   └── SkeletalMesh.cpp
│   ├── loader/
│   │   ├── ModelLoader.cpp
│   │   └── SkeletalModelLoader.cpp
│   └── scene/
│       ├── Scene.cpp
│       └── SceneSkeletal.cpp
└── shaders/
    ├── pbr.vert
    ├── pbr.frag
    └── skeletal.vert
```

---

## Build Notes

### Adding New Files to Visual Studio Project
When adding new `.cpp` files, ensure they are added to the Visual Studio project:
1. Right-click on the appropriate filter in Solution Explorer
2. Add → Existing Item
3. Select the new source files

### New Source Files to Add
```
src/animation/Skeleton.cpp
src/animation/AnimationClip.cpp
src/animation/SkeletalMeshComponent.cpp
src/mesh/SkeletalMesh.cpp
src/loader/SkeletalModelLoader.cpp
src/scene/SceneSkeletal.cpp
```

### Shader Compilation
Compile `skeletal.vert` to SPIR-V:
```bash
glslc shaders/skeletal.vert -o shaders/skeletal.vert.spv
```

---

## Dependencies
- Vulkan SDK
- FBX SDK (for model/animation loading)
- GLM (math library)
- ImGui (debug UI)
- vcpkg managed libraries
