# Milestone 1: Physics Foundation (2025-11-29)

**Goal:** Component-based physics system foundation allowing selective physics on MeshInstances.

## New Files
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

## Modified Files
- `include/scene/Scene.h` - Added component pointers to MeshInstance, PhysicsWorld member
- `src/scene/Scene.cpp` - Added `enablePhysics()`, physics update integration

## Features Implemented
- Optional physics via `shared_ptr` components (nullptr = no physics overhead)
- `Scene::enablePhysics(index, bodyType)` helper for easy setup
- RigidBodyType: Dynamic, Kinematic, Static
- Gravity application with per-object gravity scale
- Force and impulse API
- Position constraints (lock X/Y/Z axes)
- Collision layer bitmask filtering (structure only)
- Fixed timestep physics (60 Hz default)

## Usage
```cpp
// Enable physics on a mesh instance
m_Scene->enablePhysics(0, RigidBodyType::Dynamic);

// Access and customize
auto* obj = m_Scene->getMeshInstance(0);
obj->rigidBody->mass = 2.0f;
obj->rigidBody->addImpulse({0, 10, 0});
```

## TODO (Future Milestones)
- Collision detection (AABB-AABB, Sphere-Sphere, AABB-Sphere)
- Collision response (impulse-based resolution)
- Raycast and spatial queries
- Debug visualization panel
