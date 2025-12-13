# Milestone 5: Actor System and Scene Serialization (2025-12-02)

**Goal:** UE5-inspired Actor/Component architecture with scene save/load support.

**See also:** `MILESTONE_5_ACTOR_SCENE_SYSTEM.md` for full design documentation.

## New Files
```
include/core/
├── MiObject.h              - Base class with UUID, name, type info
├── MiActor.h               - Base actor class with components
├── MiComponent.h           - Base component class
├── MiSceneComponent.h      - Transform-based component with hierarchy
├── MiWorld.h               - World container for actors
├── MiTransform.h           - Transform with quaternion rotation
├── MiDelegate.h            - Event/delegate system
├── MiTypeRegistry.h        - Runtime type registration
├── JsonIO.h                - JSON serialization utilities
└── MiCore.h                - Convenience header

include/actor/
├── MiEmptyActor.h          - Empty grouping actor
└── MiStaticMeshActor.h     - Static mesh actor

include/component/
└── MiStaticMeshComponent.h - Static mesh rendering component

include/scene/
└── SceneSerializer.h       - Scene save/load API

src/core/
├── MiObject.cpp
├── MiActor.cpp
├── MiComponent.cpp
├── MiSceneComponent.cpp
├── MiWorld.cpp
├── MiTransform.cpp
├── MiTypeRegistry.cpp
└── JsonIO.cpp

src/actor/
├── MiEmptyActor.cpp
└── MiStaticMeshActor.cpp

src/component/
└── MiStaticMeshComponent.cpp

src/scene/
└── SceneSerializer.cpp
```

## Features Implemented
- **MiObject**: Base class with UUID, name, dirty tracking, serialization
- **MiActor**: Component management, transform, tags, layers, flags
- **MiComponent**: Lifecycle callbacks (onAttached, beginPlay, tick, etc.)
- **MiSceneComponent**: Hierarchical transforms, parent/child relationships
- **MiWorld**: Actor spawning, queries, update loop, serialization
- **MiTransform**: Quaternion-based rotation, matrix operations
- **MiTypeRegistry**: Runtime type creation by name (for deserialization)
- **MiDelegate**: Single and multicast delegates for events
- **SceneSerializer**: .miscene JSON format save/load

## Usage
```cpp
// Create world and spawn actors
MiWorld world;
world.initialize(renderer);

auto cube = world.spawnActor<MiStaticMeshActor>();
cube->setName("Floor");
cube->setPosition({0, -1, 0});
cube->setScale({10, 0.5f, 10});
cube->setMesh("Models/cube.fbx");

auto empty = world.spawnActor<MiEmptyActor>();
empty->setName("Waypoint");
empty->addTag("spawn_point");

// Update loop
world.beginPlay();
while (running) {
    world.tick(deltaTime);
}
world.endPlay();

// Save scene
SceneSerializer::saveScene(world, "Scenes/Level1.miscene");

// Load scene
MiWorld newWorld;
newWorld.initialize(renderer);
SceneSerializer::loadScene(newWorld, "Scenes/Level1.miscene");
```

## Scene File Format (.miscene)
```json
{
  "version": 1,
  "name": "Level1",
  "settings": {
    "gravity": [0, -9.81, 0],
    "ambientColor": [0.1, 0.1, 0.1]
  },
  "actors": [
    {
      "type": "MiStaticMeshActor",
      "id": "uuid-here",
      "name": "Floor",
      "transform": {
        "position": [0, -1, 0],
        "rotation": [1, 0, 0, 0],
        "scale": [10, 0.5, 10]
      },
      "components": [...]
    }
  ]
}
```

## Architecture Notes
- `MI_OBJECT_BODY(TypeName, TypeId)` macro for RTTI
- `MI_REGISTER_TYPE(TypeName)` macro for auto-registration
- Template-based component management (`addComponent<T>()`, `getComponent<T>()`)
- Deferred actor destruction (processed at end of frame)
- Type ID ranges: 100-199 actors, 200-299 components, 1000+ game-specific

## TODO (Future Phases)
- MiSkeletalMeshActor and MiSkeletalMeshComponent
- MiLightActor (Point, Directional, Spot)
- MiCameraActor
- Physics component refactor (MiRigidBodyComponent, MiColliderComponent)
- World Outliner debug panel
- Actor Details debug panel
- Integration with existing Scene class
