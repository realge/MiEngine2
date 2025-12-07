# MiEngine2 Actor System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Class Reference](#class-reference)
4. [Quick Start Guide](#quick-start-guide)
5. [Creating Custom Actors](#creating-custom-actors)
6. [Creating Custom Components](#creating-custom-components)
7. [Scene Serialization](#scene-serialization)
8. [Events and Delegates](#events-and-delegates)
9. [Best Practices](#best-practices)
10. [API Reference](#api-reference)

---

## Overview

The MiEngine2 Actor System is an Unreal Engine 5-inspired architecture for managing game objects. It provides:

- **Component-based design**: Attach modular functionality to actors
- **Hierarchical transforms**: Parent/child relationships with automatic transform propagation
- **Type-safe serialization**: Save and load scenes with automatic type reconstruction
- **Event system**: Decoupled communication between objects

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                           MiWorld                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │   MiActor   │  │   MiActor   │  │   MiActor   │  ...         │
│  │  ┌───────┐  │  │  ┌───────┐  │  │  ┌───────┐  │              │
│  │  │MiComp │  │  │  │MiComp │  │  │  │MiComp │  │              │
│  │  └───────┘  │  │  └───────┘  │  │  └───────┘  │              │
│  │  ┌───────┐  │  │  ┌───────┐  │  │             │              │
│  │  │MiComp │  │  │  │MiComp │  │  │             │              │
│  │  └───────┘  │  │  └───────┘  │  │             │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

### Comparison with Unreal Engine 5

| MiEngine2 | Unreal Engine 5 | Purpose |
|-----------|-----------------|---------|
| `MiObject` | `UObject` | Base class for all objects |
| `MiActor` | `AActor` | Placeable entity in world |
| `MiComponent` | `UActorComponent` | Logic-only component |
| `MiSceneComponent` | `USceneComponent` | Component with transform |
| `MiWorld` | `UWorld` | Container for all actors |

---

## Core Concepts

### 1. MiObject - The Base Class

Every object in the actor system inherits from `MiObject`. It provides:

- **UUID**: Unique identifier that persists across save/load
- **Name**: Human-readable display name
- **Type Info**: Runtime type identification
- **Serialization**: JSON save/load support
- **Dirty Tracking**: Know when objects have unsaved changes

```cpp
#include "core/MiObject.h"

// Every MiObject has:
object->getObjectId();     // UUID string
object->getName();         // Display name
object->getTypeName();     // "MiActor", "MiStaticMeshActor", etc.
object->getTypeId();       // Numeric type ID
object->isDirty();         // Has unsaved changes?
```

### 2. MiActor - Game Entities

Actors are the primary entities you place in a world. They:

- Have a transform (position, rotation, scale)
- Can contain multiple components
- Support tags and layers for filtering
- Have lifecycle callbacks (beginPlay, tick, endPlay)

```cpp
#include "core/MiActor.h"

// Actors can:
actor->setPosition({0, 5, 0});
actor->setRotation(glm::quat(...));
actor->addComponent<MyComponent>();
actor->addTag("enemy");
actor->setLayer(1);
```

### 3. MiComponent - Modular Functionality

Components add functionality to actors. Two types:

- **MiComponent**: Logic only (no transform)
- **MiSceneComponent**: Has transform, can have parent/children

```cpp
#include "core/MiComponent.h"
#include "core/MiSceneComponent.h"

// Components have:
component->getOwner();        // Parent actor
component->isEnabled();       // Is active?
component->tick(deltaTime);   // Called every frame (if tickable)
```

### 4. MiWorld - The Container

The world holds all actors and manages their lifecycle:

```cpp
#include "core/MiWorld.h"

MiWorld world;
world.initialize(renderer);

// Spawn actors
auto actor = world.spawnActor<MiStaticMeshActor>();

// Query actors
auto found = world.findActorByName("Player");
auto enemies = world.findActorsByTag("enemy");

// Update loop
world.beginPlay();
world.tick(deltaTime);  // Call every frame
world.endPlay();
```

### 5. MiTransform - Position, Rotation, Scale

Transforms use quaternions for rotation (no gimbal lock):

```cpp
#include "core/MiTransform.h"

MiTransform transform;
transform.position = {0, 5, 0};
transform.rotation = glm::quat(1, 0, 0, 0);  // Identity
transform.scale = {1, 1, 1};

// Helpers
transform.setEulerAngles({0, glm::radians(90.0f), 0});
glm::vec3 forward = transform.getForward();
glm::mat4 matrix = transform.getMatrix();
```

---

## Class Reference

### MiObject

**Purpose**: Base class for all engine objects.

**Header**: `include/core/MiObject.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getObjectId()` | Get unique UUID string |
| `getName()` / `setName()` | Get/set display name |
| `getTypeName()` | Get type name as string |
| `getTypeId()` | Get numeric type ID |
| `isA<T>()` | Check if object is of type T |
| `serialize()` | Save to JSON |
| `deserialize()` | Load from JSON |
| `isDirty()` | Check for unsaved changes |
| `markDirty()` | Mark as modified |
| `onCreated()` | Called after construction |
| `onDestroyed()` | Called before destruction |

**Example**:

```cpp
class MyObject : public MiObject {
    MI_OBJECT_BODY(MyObject, 1001)  // Type name and ID

public:
    void serialize(JsonWriter& writer) const override {
        MiObject::serialize(writer);
        writer.writeInt("health", m_Health);
    }

    void deserialize(const JsonReader& reader) override {
        MiObject::deserialize(reader);
        m_Health = reader.getInt("health", 100);
    }

private:
    int m_Health = 100;
};
```

---

### MiActor

**Purpose**: Base class for all placeable game entities.

**Header**: `include/core/MiActor.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getWorld()` | Get owning world |
| `getPosition()` / `setPosition()` | World position |
| `getRotation()` / `setRotation()` | World rotation (quaternion) |
| `getScale()` / `setScale()` | World scale |
| `getTransform()` / `setTransform()` | Full transform |
| `addComponent<T>()` | Add component of type T |
| `getComponent<T>()` | Get first component of type T |
| `getComponents<T>()` | Get all components of type T |
| `hasComponent<T>()` | Check if has component |
| `removeComponent()` | Remove a component |
| `getRootComponent()` | Get root scene component |
| `addTag()` / `hasTag()` | Tag management |
| `getLayer()` / `setLayer()` | Layer for grouping |
| `isHidden()` / `setHidden()` | Visibility |
| `isStatic()` | Won't move at runtime |
| `destroy()` | Mark for destruction |
| `beginPlay()` | Called when game starts |
| `tick(deltaTime)` | Called every frame |
| `endPlay()` | Called when game ends |

**Flags**:

```cpp
enum class ActorFlags {
    None,
    Hidden,      // Don't render
    Transient,   // Don't save to scene
    EditorOnly,  // Editor only
    Static,      // Won't move (optimization)
    Selected     // Selected in editor
};
```

**Example**:

```cpp
// Spawn and configure
auto enemy = world.spawnActor<MiActor>();
enemy->setName("Goblin");
enemy->setPosition({10, 0, 5});
enemy->addTag("enemy");
enemy->addTag("hostile");
enemy->setLayer(2);  // Enemy layer

// Add components
auto health = enemy->addComponent<HealthComponent>();
auto ai = enemy->addComponent<AIComponent>();

// Check tags
if (enemy->hasTag("enemy")) {
    // Handle enemy logic
}

// Destroy later
enemy->destroy();  // Removed at end of frame
```

---

### MiComponent

**Purpose**: Base class for logic-only components (no transform).

**Header**: `include/core/MiComponent.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getOwner()` | Get owning actor |
| `isEnabled()` / `setEnabled()` | Enable/disable |
| `isTickable()` | Should tick() be called? |
| `getTickPriority()` | Order of tick (lower = earlier) |
| `onAttached()` | Called when added to actor |
| `onDetached()` | Called when removed |
| `beginPlay()` | Called when game starts |
| `tick(deltaTime)` | Called every frame |
| `endPlay()` | Called when game ends |

**Example**:

```cpp
class HealthComponent : public MiComponent {
    MI_OBJECT_BODY(HealthComponent, 300)

public:
    float getHealth() const { return m_Health; }

    void takeDamage(float amount) {
        m_Health -= amount;
        if (m_Health <= 0) {
            getOwner()->destroy();
        }
    }

    void serialize(JsonWriter& writer) const override {
        MiComponent::serialize(writer);
        writer.writeFloat("health", m_Health);
        writer.writeFloat("maxHealth", m_MaxHealth);
    }

    void deserialize(const JsonReader& reader) override {
        MiComponent::deserialize(reader);
        m_Health = reader.getFloat("health", 100.0f);
        m_MaxHealth = reader.getFloat("maxHealth", 100.0f);
    }

private:
    float m_Health = 100.0f;
    float m_MaxHealth = 100.0f;
};
```

---

### MiSceneComponent

**Purpose**: Component with a transform that can have parent/child hierarchy.

**Header**: `include/core/MiSceneComponent.h`

**Inherits**: `MiComponent`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getLocalTransform()` | Transform relative to parent |
| `setLocalPosition/Rotation/Scale()` | Set local transform parts |
| `getWorldTransform()` | Computed world transform |
| `getWorldPosition/Rotation/Scale()` | World transform parts |
| `setWorldPosition/Rotation()` | Set in world space |
| `getForwardVector()` | Forward direction |
| `getRightVector()` | Right direction |
| `getUpVector()` | Up direction |
| `attachTo(parent)` | Attach to another component |
| `detachFromParent()` | Detach from parent |
| `getParent()` | Get parent component |
| `getChildren()` | Get child components |
| `isVisible()` / `setVisible()` | Visibility |
| `lookAt(target)` | Face a point |

**Example**:

```cpp
// Create hierarchy
auto root = actor->addComponent<MiSceneComponent>();
auto arm = actor->addComponent<MiSceneComponent>();
auto hand = actor->addComponent<MiSceneComponent>();

arm->attachTo(root.get());
hand->attachTo(arm.get());

// Transforms are relative to parent
arm->setLocalPosition({1, 0, 0});   // 1 unit right of root
hand->setLocalPosition({0.5f, 0, 0}); // 0.5 units right of arm

// World position is computed automatically
glm::vec3 handWorldPos = hand->getWorldPosition();  // {1.5, 0, 0}

// Moving parent moves children
root->setLocalPosition({0, 5, 0});
// Now hand world position is {1.5, 5, 0}
```

---

### MiWorld

**Purpose**: Container for all actors, manages lifecycle and queries.

**Header**: `include/core/MiWorld.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `initialize(renderer)` | Initialize world |
| `shutdown()` | Clean up world |
| `spawnActor<T>()` | Create actor of type T |
| `spawnActorByTypeName()` | Create by type name string |
| `destroyActor()` | Queue actor for destruction |
| `destroyAllActors()` | Destroy everything |
| `findActorById()` | Find by UUID |
| `findActorByName()` | Find by name |
| `findActorsByTag()` | Find all with tag |
| `findActorsByLayer()` | Find all on layer |
| `findActorsOfType<T>()` | Find all of type T |
| `getAllActors()` | Get all actors |
| `getActorCount()` | Number of actors |
| `forEachActor()` | Iterate all actors |
| `beginPlay()` | Start simulation |
| `tick(deltaTime)` | Update world |
| `endPlay()` | Stop simulation |
| `isPlaying()` | Is simulation running? |
| `getSettings()` | World settings |
| `hasUnsavedChanges()` | Any dirty actors? |

**WorldSettings**:

```cpp
struct WorldSettings {
    glm::vec3 gravity = {0, -9.81f, 0};
    float physicsTimeStep = 1.0f / 60.0f;
    bool enablePhysics = true;
    glm::vec3 ambientColor = {0.1f, 0.1f, 0.1f};
    std::string skyboxPath;
};
```

**Example**:

```cpp
// Create and initialize
MiWorld world;
world.initialize(renderer);

// Configure settings
WorldSettings settings;
settings.gravity = {0, -20.0f, 0};  // Stronger gravity
world.setSettings(settings);

// Spawn actors
auto player = world.spawnActor<MiCharacterActor>();
player->setName("Player");
player->setPosition({0, 1, 0});

for (int i = 0; i < 10; ++i) {
    auto enemy = world.spawnActor<MiEnemyActor>();
    enemy->setName("Enemy_" + std::to_string(i));
    enemy->addTag("enemy");
}

// Query actors
auto allEnemies = world.findActorsByTag("enemy");
std::cout << "Spawned " << allEnemies.size() << " enemies\n";

// Game loop
world.beginPlay();

while (gameRunning) {
    float dt = getDeltaTime();
    world.tick(dt);
    renderWorld(world);
}

world.endPlay();
world.shutdown();
```

---

### MiTransform

**Purpose**: Represents position, rotation (quaternion), and scale.

**Header**: `include/core/MiTransform.h`

**Key Members**:

| Member/Method | Description |
|---------------|-------------|
| `position` | glm::vec3 position |
| `rotation` | glm::quat rotation |
| `scale` | glm::vec3 scale |
| `getMatrix()` | Get 4x4 transformation matrix |
| `setFromMatrix()` | Set from matrix |
| `getEulerAngles()` | Get rotation as euler (radians) |
| `setEulerAngles()` | Set rotation from euler |
| `getForward/Right/Up()` | Direction vectors |
| `lookAt(target)` | Face a point |
| `transformPoint()` | Transform point to world |
| `inverseTransformPoint()` | Transform point to local |
| `operator*()` | Combine transforms |
| `inverse()` | Get inverse transform |
| `lerp()` | Interpolate between transforms |

**Example**:

```cpp
MiTransform transform;

// Set position
transform.position = {10, 5, -3};

// Set rotation (quaternion)
transform.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));

// Or use euler angles
transform.setEulerAngles({0, glm::radians(45.0f), 0});

// Set scale
transform.scale = {2, 2, 2};

// Get matrix for rendering
glm::mat4 modelMatrix = transform.getMatrix();

// Direction vectors
glm::vec3 forward = transform.getForward();

// Transform a point
glm::vec3 localPoint = {1, 0, 0};
glm::vec3 worldPoint = transform.transformPoint(localPoint);

// Interpolate for animation
MiTransform a, b;
MiTransform interpolated = MiTransform::lerp(a, b, 0.5f);
```

---

### MiStaticMeshActor

**Purpose**: Actor that displays a static (non-animated) mesh.

**Header**: `include/actor/MiStaticMeshActor.h`

**Inherits**: `MiActor`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getMeshComponent()` | Get the mesh component |
| `setMesh(path)` | Set mesh by asset path |
| `setMesh(meshPtr)` | Set mesh directly |
| `getMesh()` | Get current mesh |
| `setMaterial()` | Set PBR material |
| `getMaterial()` | Get material |
| `setBaseColor()` | Set albedo color |
| `setMetallic()` | Set metallic value |
| `setRoughness()` | Set roughness value |
| `setCastShadows()` | Enable/disable shadow casting |

**Example**:

```cpp
auto cube = world.spawnActor<MiStaticMeshActor>();
cube->setName("RedCube");
cube->setPosition({0, 1, 0});
cube->setScale({2, 2, 2});

// Set mesh
cube->setMesh("Assets/Models/cube.fbx");

// Set material properties
cube->setBaseColor({1, 0, 0});  // Red
cube->setMetallic(0.0f);
cube->setRoughness(0.5f);
cube->setCastShadows(true);

// Or set full material
Material mat;
mat.baseColor = {0, 1, 0};
mat.metallic = 0.8f;
mat.roughness = 0.2f;
cube->setMaterial(mat);
```

---

### MiStaticMeshComponent

**Purpose**: Component that renders a static mesh.

**Header**: `include/component/MiStaticMeshComponent.h`

**Inherits**: `MiSceneComponent`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getMesh()` / `setMesh()` | Mesh pointer |
| `setMeshByPath()` | Load mesh by path |
| `getMeshAssetPath()` | Get asset path |
| `hasMesh()` | Check if mesh loaded |
| `getMaterial()` / `setMaterial()` | PBR material |
| `setBaseColor/Metallic/Roughness()` | Material shortcuts |
| `shouldRender()` | Should be rendered? |
| `getCastShadows()` / `setCastShadows()` | Shadow casting |
| `getReceiveShadows()` / `setReceiveShadows()` | Shadow receiving |
| `getLocalBoundsMin/Max()` | Bounding box |

**Example**:

```cpp
auto actor = world.spawnActor<MiActor>();

// Add mesh component
auto mesh = actor->addComponent<MiStaticMeshComponent>();
mesh->setMeshByPath("Assets/Models/tree.fbx");
mesh->setBaseColor({0.2f, 0.8f, 0.2f});

// Add another mesh component (actor can have multiple)
auto leaves = actor->addComponent<MiStaticMeshComponent>();
leaves->setMeshByPath("Assets/Models/leaves.fbx");
leaves->setLocalPosition({0, 2, 0});  // Offset from root
leaves->attachTo(mesh.get());  // Parent to trunk
```

---

### MiEmptyActor

**Purpose**: Empty actor for organization and grouping.

**Header**: `include/actor/MiEmptyActor.h`

**Inherits**: `MiActor`

**Use Cases**:
- Grouping other actors in hierarchy
- Waypoints and markers
- Spawn points
- Transform parents

**Example**:

```cpp
// Create spawn points
for (int i = 0; i < 4; ++i) {
    auto spawnPoint = world.spawnActor<MiEmptyActor>();
    spawnPoint->setName("SpawnPoint_" + std::to_string(i));
    spawnPoint->addTag("spawn_point");
    spawnPoint->setPosition(spawnPositions[i]);
}

// Later, find spawn points
auto spawnPoints = world.findActorsByTag("spawn_point");
auto& randomSpawn = spawnPoints[rand() % spawnPoints.size()];
player->setPosition(randomSpawn->getPosition());
```

---

### MiTypeRegistry

**Purpose**: Runtime type registration for serialization.

**Header**: `include/core/MiTypeRegistry.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `getInstance()` | Get singleton |
| `registerType<T>()` | Register a type |
| `create(typeName)` | Create by type name |
| `createById(typeId)` | Create by type ID |
| `isRegistered()` | Check if registered |
| `getTypeInfo()` | Get type metadata |
| `getRegisteredTypeNames()` | List all types |

**Macros**:

```cpp
// In header - declare type info
MI_OBJECT_BODY(MyClass, 1001)

// In cpp - auto-register type
MI_REGISTER_TYPE(MyClass)

// With parent type
MI_REGISTER_TYPE_WITH_PARENT(MyDerivedClass, MyBaseClass)
```

**Example**:

```cpp
// Check if type exists
auto& registry = MiTypeRegistry::getInstance();
if (registry.isRegistered("MiStaticMeshActor")) {
    // Create instance by name
    auto obj = registry.create("MiStaticMeshActor");
    auto actor = std::dynamic_pointer_cast<MiActor>(obj);
}

// List all actor types
for (const auto& typeName : registry.getRegisteredTypeNames()) {
    std::cout << "Registered: " << typeName << "\n";
}
```

---

### MiDelegate

**Purpose**: Event system for decoupled communication.

**Header**: `include/core/MiDelegate.h`

**Types**:

| Type | Description |
|------|-------------|
| `MiSingleDelegate<Args...>` | One listener only |
| `MiMulticastDelegate<Args...>` | Multiple listeners |
| `MiDelegate<Args...>` | Alias for multicast |
| `MiEvent<Args...>` | Alias for multicast |

**Key Members (Multicast)**:

| Method | Description |
|--------|-------------|
| `add(func)` | Add listener, returns handle |
| `add(obj, &Class::Method)` | Add member function |
| `remove(handle)` | Remove by handle |
| `clear()` | Remove all |
| `isBound()` | Has any listeners? |
| `broadcast(args...)` | Call all listeners |

**Example**:

```cpp
// Define events in a component
class HealthComponent : public MiComponent {
public:
    MiEvent<float, float> OnHealthChanged;  // oldHealth, newHealth
    MiEvent<MiActor*> OnDeath;              // killer

    void takeDamage(float amount, MiActor* attacker) {
        float oldHealth = m_Health;
        m_Health = std::max(0.0f, m_Health - amount);

        OnHealthChanged.broadcast(oldHealth, m_Health);

        if (m_Health <= 0) {
            OnDeath.broadcast(attacker);
        }
    }

private:
    float m_Health = 100.0f;
};

// Subscribe to events
auto health = actor->getComponent<HealthComponent>();

// Lambda
DelegateHandle handle = health->OnHealthChanged.add([](float old, float current) {
    std::cout << "Health: " << old << " -> " << current << "\n";
});

// Member function
health->OnDeath.add(this, &GameManager::onEnemyDeath);

// Unsubscribe
health->OnHealthChanged.remove(handle);

// RAII handle (auto-unsubscribes)
MiDelegateHandle autoHandle(health->OnDeath,
    health->OnDeath.add([](MiActor* killer) {
        std::cout << "Died!\n";
    }));
// Unsubscribes when autoHandle goes out of scope
```

---

### SceneSerializer

**Purpose**: Save and load scenes to/from .miscene files.

**Header**: `include/scene/SceneSerializer.h`

**Key Members**:

| Method | Description |
|--------|-------------|
| `saveScene(world, path)` | Save world to file |
| `saveScene(world, path, metadata)` | Save with metadata |
| `loadScene(world, path)` | Load world from file |
| `peekScene(path)` | Get metadata without loading |
| `validateScene(path)` | Check if file is valid |
| `isSceneFile(path)` | Check extension |
| `createDefaultScene(world)` | Create empty scene |

**SceneMetadata**:

```cpp
struct SceneMetadata {
    std::string name;
    std::string description;
    std::string author;
    uint32_t version;
    uint64_t createdTime;
    uint64_t modifiedTime;
};
```

**SceneResult**:

```cpp
struct SceneResult {
    bool success;
    std::string errorMessage;
    SceneMetadata metadata;
    size_t actorCount;
};
```

**Example**:

```cpp
// Save scene
SceneMetadata metadata;
metadata.name = "Level1";
metadata.description = "First level of the game";
metadata.author = "GameDev";

auto result = SceneSerializer::saveScene(world, "Scenes/Level1.miscene", metadata);
if (!result.success) {
    std::cerr << "Save failed: " << result.errorMessage << "\n";
}

// Load scene
MiWorld world;
world.initialize(renderer);

auto loadResult = SceneSerializer::loadScene(world, "Scenes/Level1.miscene");
if (loadResult.success) {
    std::cout << "Loaded scene: " << loadResult.metadata.name << "\n";
    std::cout << "Actors: " << loadResult.actorCount << "\n";
} else {
    std::cerr << "Load failed: " << loadResult.errorMessage << "\n";
}

// Peek at scene without loading
auto peekResult = SceneSerializer::peekScene("Scenes/Level1.miscene");
if (peekResult.success) {
    std::cout << "Scene name: " << peekResult.metadata.name << "\n";
}
```

---

### JsonWriter / JsonReader

**Purpose**: JSON serialization utilities.

**Header**: `include/core/JsonIO.h`

**JsonWriter Methods**:

| Method | Description |
|--------|-------------|
| `beginObject()` / `endObject()` | Object braces |
| `beginArray(key)` / `endArray()` | Array brackets |
| `writeString(key, value)` | Write string |
| `writeInt/UInt/Float/Bool()` | Write primitives |
| `writeVec2/Vec3/Vec4()` | Write GLM vectors |
| `writeQuat()` | Write quaternion |
| `writeMat4()` | Write 4x4 matrix |
| `toString()` | Get JSON string |
| `saveToFile(path)` | Write to file |

**JsonReader Methods**:

| Method | Description |
|--------|-------------|
| `loadFromFile(path)` | Read from file |
| `loadFromString(json)` | Parse string |
| `hasKey(key)` | Check if key exists |
| `getString/Int/Float/Bool()` | Read primitives |
| `getVec2/Vec3/Vec4()` | Read GLM vectors |
| `getQuat()` | Read quaternion |
| `getArray(key)` | Get array of readers |
| `getObject(key)` | Get nested object |

**Example**:

```cpp
// Writing
JsonWriter writer;
writer.beginObject();
writer.writeString("name", "Player");
writer.writeInt("level", 5);
writer.writeVec3("position", {10, 5, 0});
writer.beginArray("inventory");
writer.writeArrayString("Sword");
writer.writeArrayString("Shield");
writer.endArray();
writer.endObject();

writer.saveToFile("save.json");

// Reading
JsonReader reader;
reader.loadFromFile("save.json");

std::string name = reader.getString("name");
int level = reader.getInt("level", 1);
glm::vec3 pos = reader.getVec3("position");

auto inventory = reader.getStringArray("inventory");
for (const auto& item : inventory) {
    std::cout << "Has: " << item << "\n";
}
```

---

## Quick Start Guide

### Minimal Example

```cpp
#include "core/MiCore.h"
#include "actor/MiStaticMeshActor.h"
#include "actor/MiEmptyActor.h"
#include "scene/SceneSerializer.h"

using namespace MiEngine;

int main() {
    // Create world
    MiWorld world;
    world.initialize(nullptr);  // nullptr if no renderer yet

    // Spawn actors
    auto floor = world.spawnActor<MiStaticMeshActor>();
    floor->setName("Floor");
    floor->setPosition({0, 0, 0});
    floor->setScale({10, 0.5f, 10});
    floor->setMesh("Models/cube.fbx");
    floor->setBaseColor({0.5f, 0.5f, 0.5f});

    auto cube = world.spawnActor<MiStaticMeshActor>();
    cube->setName("Cube");
    cube->setPosition({0, 2, 0});
    cube->setMesh("Models/cube.fbx");
    cube->setBaseColor({1, 0, 0});

    auto spawnPoint = world.spawnActor<MiEmptyActor>();
    spawnPoint->setName("PlayerSpawn");
    spawnPoint->setPosition({0, 1, -5});
    spawnPoint->addTag("spawn");

    // Save scene
    SceneSerializer::saveScene(world, "Scenes/TestScene.miscene");

    // Game loop
    world.beginPlay();

    while (running) {
        world.tick(deltaTime);

        // Your rendering here
        for (const auto& actor : world.getAllActors()) {
            if (auto meshActor = std::dynamic_pointer_cast<MiStaticMeshActor>(actor)) {
                renderMesh(meshActor->getMesh(), meshActor->getTransform().getMatrix());
            }
        }
    }

    world.endPlay();
    world.shutdown();

    return 0;
}
```

---

## Creating Custom Actors

### Step 1: Create Header

```cpp
// include/actor/MyEnemyActor.h
#pragma once

#include "core/MiActor.h"

namespace MiEngine {

class MyEnemyActor : public MiActor {
    MI_OBJECT_BODY(MyEnemyActor, 1001)  // Unique ID >= 1000 for game types

public:
    MyEnemyActor();

    // Custom API
    void setTarget(MiActor* target) { m_Target = target; }
    MiActor* getTarget() const { return m_Target; }

    float getHealth() const { return m_Health; }
    void takeDamage(float amount);

    // Override lifecycle
    void beginPlay() override;
    void tick(float deltaTime) override;

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    void createDefaultComponents() override;

private:
    MiActor* m_Target = nullptr;
    float m_Health = 100.0f;
    float m_Speed = 5.0f;
};

} // namespace MiEngine
```

### Step 2: Create Implementation

```cpp
// src/actor/MyEnemyActor.cpp
#include "actor/MyEnemyActor.h"
#include "core/MiTypeRegistry.h"
#include "component/MiStaticMeshComponent.h"

namespace MiEngine {

MyEnemyActor::MyEnemyActor() : MiActor() {
    setName("Enemy");
    addTag("enemy");
}

void MyEnemyActor::createDefaultComponents() {
    // Add mesh component
    auto mesh = addComponent<MiStaticMeshComponent>();
    mesh->setMeshByPath("Models/enemy.fbx");
    setRootComponent(mesh);
}

void MyEnemyActor::beginPlay() {
    MiActor::beginPlay();
    // Initialize AI, etc.
}

void MyEnemyActor::tick(float deltaTime) {
    MiActor::tick(deltaTime);

    // Move toward target
    if (m_Target) {
        glm::vec3 direction = m_Target->getPosition() - getPosition();
        if (glm::length(direction) > 1.0f) {
            direction = glm::normalize(direction);
            setPosition(getPosition() + direction * m_Speed * deltaTime);
        }
    }
}

void MyEnemyActor::takeDamage(float amount) {
    m_Health -= amount;
    if (m_Health <= 0) {
        destroy();
    }
}

void MyEnemyActor::serialize(JsonWriter& writer) const {
    MiActor::serialize(writer);
    writer.writeFloat("health", m_Health);
    writer.writeFloat("speed", m_Speed);
}

void MyEnemyActor::deserialize(const JsonReader& reader) {
    MiActor::deserialize(reader);
    m_Health = reader.getFloat("health", 100.0f);
    m_Speed = reader.getFloat("speed", 5.0f);
}

// Register the type
MI_REGISTER_TYPE(MyEnemyActor)

} // namespace MiEngine
```

---

## Creating Custom Components

### Step 1: Create Header

```cpp
// include/component/RotatorComponent.h
#pragma once

#include "core/MiComponent.h"
#include <glm/glm.hpp>

namespace MiEngine {

class RotatorComponent : public MiComponent {
    MI_OBJECT_BODY(RotatorComponent, 1100)

public:
    RotatorComponent();

    // Configuration
    void setRotationSpeed(const glm::vec3& speed) { m_RotationSpeed = speed; }
    glm::vec3 getRotationSpeed() const { return m_RotationSpeed; }

    // Override to enable ticking
    bool isTickable() const override { return true; }

    void tick(float deltaTime) override;

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    glm::vec3 m_RotationSpeed = {0, 1, 0};  // Radians per second
};

} // namespace MiEngine
```

### Step 2: Create Implementation

```cpp
// src/component/RotatorComponent.cpp
#include "component/RotatorComponent.h"
#include "core/MiActor.h"
#include "core/MiTypeRegistry.h"

namespace MiEngine {

RotatorComponent::RotatorComponent() : MiComponent() {
    setName("Rotator");
}

void RotatorComponent::tick(float deltaTime) {
    if (MiActor* owner = getOwner()) {
        glm::vec3 euler = owner->getEulerAngles();
        euler += m_RotationSpeed * deltaTime;
        owner->setEulerAngles(euler);
    }
}

void RotatorComponent::serialize(JsonWriter& writer) const {
    MiComponent::serialize(writer);
    writer.writeVec3("rotationSpeed", m_RotationSpeed);
}

void RotatorComponent::deserialize(const JsonReader& reader) {
    MiComponent::deserialize(reader);
    m_RotationSpeed = reader.getVec3("rotationSpeed", {0, 1, 0});
}

MI_REGISTER_TYPE(RotatorComponent)

} // namespace MiEngine
```

### Usage

```cpp
auto cube = world.spawnActor<MiStaticMeshActor>();
cube->setMesh("Models/cube.fbx");

auto rotator = cube->addComponent<RotatorComponent>();
rotator->setRotationSpeed({0, glm::radians(45.0f), 0});  // 45 deg/sec on Y
```

---

## Scene Serialization

### File Format (.miscene)

```json
{
  "version": 1,
  "name": "MyLevel",
  "description": "A test level",
  "author": "Developer",
  "createdTime": 1701500000,
  "modifiedTime": 1701500000,
  "settings": {
    "gravity": [0.0, -9.81, 0.0],
    "physicsTimeStep": 0.016667,
    "enablePhysics": true,
    "ambientColor": [0.1, 0.1, 0.1],
    "skybox": ""
  },
  "actors": [
    {
      "type": "MiStaticMeshActor",
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Floor",
      "flags": 8,
      "layer": 0,
      "tags": ["environment"],
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation": [1.0, 0.0, 0.0, 0.0],
        "scale": [10.0, 0.5, 10.0]
      },
      "components": [
        {
          "type": "MiStaticMeshComponent",
          "id": "550e8400-e29b-41d4-a716-446655440001",
          "name": "StaticMeshComponent",
          "enabled": true,
          "meshAsset": "Models/cube.fbx",
          "castShadows": true,
          "receiveShadows": true,
          "material": {
            "baseColor": [0.5, 0.5, 0.5],
            "metallic": 0.0,
            "roughness": 0.8,
            "emissiveStrength": 0.0
          }
        }
      ]
    }
  ]
}
```

### Custom Serialization

Override `serialize` and `deserialize` in your classes:

```cpp
void MyActor::serialize(JsonWriter& writer) const {
    // Always call parent first
    MiActor::serialize(writer);

    // Add custom data
    writer.writeFloat("customValue", m_CustomValue);
    writer.writeString("customString", m_CustomString);

    // Nested object
    writer.beginObject("customObject");
    writer.writeInt("x", m_Data.x);
    writer.writeInt("y", m_Data.y);
    writer.endObject();

    // Array
    writer.beginArray("items");
    for (const auto& item : m_Items) {
        writer.writeArrayString(item);
    }
    writer.endArray();
}

void MyActor::deserialize(const JsonReader& reader) {
    // Always call parent first
    MiActor::deserialize(reader);

    // Read custom data
    m_CustomValue = reader.getFloat("customValue", 0.0f);
    m_CustomString = reader.getString("customString", "default");

    // Nested object
    JsonReader objReader = reader.getObject("customObject");
    if (objReader.isValid()) {
        m_Data.x = objReader.getInt("x", 0);
        m_Data.y = objReader.getInt("y", 0);
    }

    // Array
    m_Items = reader.getStringArray("items");
}
```

---

## Events and Delegates

### Defining Events

```cpp
class PlayerActor : public MiActor {
public:
    // Events
    MiEvent<int> OnScoreChanged;           // newScore
    MiEvent<> OnDeath;                      // no args
    MiEvent<MiActor*, float> OnDamaged;    // attacker, damage

    void addScore(int points) {
        m_Score += points;
        OnScoreChanged.broadcast(m_Score);
    }

    void takeDamage(float amount, MiActor* attacker) {
        OnDamaged.broadcast(attacker, amount);
        m_Health -= amount;
        if (m_Health <= 0) {
            OnDeath.broadcast();
        }
    }
};
```

### Subscribing to Events

```cpp
// Lambda
player->OnScoreChanged.add([](int score) {
    updateUI(score);
});

// Member function
player->OnDeath.add(this, &GameManager::onPlayerDeath);

// Store handle for later removal
DelegateHandle handle = player->OnDamaged.add([](MiActor* attacker, float dmg) {
    showDamageNumber(dmg);
});

// Remove later
player->OnDamaged.remove(handle);
```

### RAII Subscription

```cpp
class UIManager {
public:
    void bindToPlayer(PlayerActor* player) {
        // Auto-unsubscribes when UIManager is destroyed
        m_ScoreHandle = MiDelegateHandle(
            player->OnScoreChanged,
            player->OnScoreChanged.add([this](int score) {
                updateScoreDisplay(score);
            })
        );
    }

private:
    MiDelegateHandle m_ScoreHandle;
};
```

---

## Best Practices

### 1. Use Components for Reusability

```cpp
// Good: Reusable component
class HealthComponent : public MiComponent { ... };

// Bad: Health in actor (not reusable)
class EnemyActor : public MiActor {
    float m_Health;  // Can't reuse for player
};
```

### 2. Use Tags for Filtering

```cpp
// Good: Query by tag
auto enemies = world.findActorsByTag("enemy");

// Bad: Check type for each actor
for (auto& actor : world.getAllActors()) {
    if (dynamic_cast<EnemyActor*>(actor.get())) { ... }
}
```

### 3. Use Layers for Grouping

```cpp
// Define layers
constexpr uint32_t LAYER_DEFAULT = 0;
constexpr uint32_t LAYER_PLAYER = 1;
constexpr uint32_t LAYER_ENEMY = 2;
constexpr uint32_t LAYER_PROJECTILE = 3;

// Assign layers
player->setLayer(LAYER_PLAYER);
enemy->setLayer(LAYER_ENEMY);

// Query by layer
auto enemies = world.findActorsByLayer(LAYER_ENEMY);
```

### 4. Use Events for Decoupling

```cpp
// Good: Event-based communication
health->OnDeath.add([](){ playDeathAnimation(); });

// Bad: Direct coupling
void takeDamage(float amount) {
    m_Health -= amount;
    if (m_Health <= 0) {
        m_AnimationSystem->playDeathAnimation();  // Tight coupling
    }
}
```

### 5. Mark Static Actors

```cpp
// Static actors can be optimized
floor->addFlags(ActorFlags::Static);
walls->addFlags(ActorFlags::Static);
```

### 6. Use Transient for Temporary Actors

```cpp
// Won't be saved to scene
auto explosion = world.spawnActor<ExplosionActor>();
explosion->addFlags(ActorFlags::Transient);
```

### 7. Type ID Conventions

```
1-99:       Reserved
100-199:    Engine actors
200-299:    Engine components (base)
300-399:    Physics components
400-499:    Rendering components
500-999:    Engine reserved
1000+:      Game-specific types
```

---

## API Reference

### Type ID Quick Reference

| Type | ID |
|------|-----|
| MiWorld | 50 |
| MiActor | 100 |
| MiStaticMeshActor | 101 |
| MiSkeletalMeshActor | 102 |
| MiLightActor | 103 |
| MiCameraActor | 104 |
| MiEmptyActor | 105 |
| MiComponent | 200 |
| MiSceneComponent | 201 |
| MiStaticMeshComponent | 210 |

### Include Headers

```cpp
// Everything
#include "core/MiCore.h"

// Individual
#include "core/MiObject.h"
#include "core/MiActor.h"
#include "core/MiComponent.h"
#include "core/MiSceneComponent.h"
#include "core/MiWorld.h"
#include "core/MiTransform.h"
#include "core/MiDelegate.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"

// Actors
#include "actor/MiEmptyActor.h"
#include "actor/MiStaticMeshActor.h"

// Components
#include "component/MiStaticMeshComponent.h"

// Serialization
#include "scene/SceneSerializer.h"
```

---

## Troubleshooting

### Actor not appearing in scene after load

Make sure your actor type is registered:
```cpp
MI_REGISTER_TYPE(MyActorType)
```

### Component tick not being called

Override `isTickable()` to return true:
```cpp
bool isTickable() const override { return true; }
```

### Transform not updating children

Call `markTransformDirty()` after modifying transform directly:
```cpp
m_LocalTransform.position = newPos;
markTransformDirty();
```

### Serialization not saving custom data

Make sure to call parent class methods:
```cpp
void serialize(JsonWriter& writer) const override {
    MiActor::serialize(writer);  // Don't forget this!
    // Your data...
}
```

---

*Documentation generated for MiEngine2 v2.0.0 - Actor System Milestone 5*
