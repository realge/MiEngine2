# Milestone 5: Actor System and Scene Serialization

**Goal:** Implement an Unreal Engine 5-inspired Actor/Component architecture with full scene serialization support.

**Date:** 2025-12-02

---

## Overview

This milestone introduces a proper game object hierarchy inspired by Unreal Engine 5:
- **MiObject**: Base class for all engine objects (like UObject)
- **MiActor**: Base class for all placeable objects in the world (like AActor)
- **MiComponent**: Base class for modular functionality (like UActorComponent)
- **MiSceneComponent**: Components with transforms (like USceneComponent)
- **MiWorld**: Container for all actors (like UWorld)
- **Scene Serialization**: JSON-based .miscene format

---

## Class Hierarchy

```
MiObject (Base class with UUID, name, serialization)
├── MiActor (Placeable entity with transform and components)
│   ├── MiStaticMeshActor (Actor with static mesh)
│   ├── MiSkeletalMeshActor (Actor with skeletal mesh + animations)
│   ├── MiLightActor (Point/Directional/Spot lights)
│   ├── MiCameraActor (Camera in scene)
│   └── MiEmptyActor (Empty actor for grouping/organization)
│
└── MiComponent (Modular functionality attached to actors)
    ├── MiSceneComponent (Has transform, can have children)
    │   ├── MiStaticMeshComponent (Renders static mesh)
    │   ├── MiSkeletalMeshComponent (Renders skeletal mesh)
    │   ├── MiLightComponent (Light source)
    │   │   ├── MiPointLightComponent
    │   │   ├── MiDirectionalLightComponent
    │   │   └── MiSpotLightComponent
    │   └── MiCameraComponent (Camera view)
    │
    └── MiActorComponent (No transform - logic only)
        ├── MiRigidBodyComponent (Physics body - existing, refactored)
        ├── MiColliderComponent (Collision shape - existing, refactored)
        └── MiAudioComponent (Sound playback - future)
```

---

## New Files

```
include/core/
├── MiObject.h              - Base object with UUID, name, type info
├── MiActor.h               - Base actor class with components
├── MiComponent.h           - Base component class
├── MiSceneComponent.h      - Transform-based component
├── MiWorld.h               - World container for actors
├── MiObjectFactory.h       - Factory for creating objects by type name
└── MiTypeRegistry.h        - Runtime type information registry

include/actor/
├── MiStaticMeshActor.h     - Static mesh actor
├── MiSkeletalMeshActor.h   - Skeletal mesh actor
├── MiLightActor.h          - Light actors (point, directional, spot)
├── MiCameraActor.h         - Camera actor
└── MiEmptyActor.h          - Empty grouping actor

include/component/
├── MiStaticMeshComponent.h     - Static mesh rendering
├── MiSkeletalMeshComponent.h   - Skeletal mesh + animation
├── MiLightComponent.h          - Light source components
├── MiCameraComponent.h         - Camera component
├── MiRigidBodyComponent.h      - Physics rigid body (refactored)
└── MiColliderComponent.h       - Collision shapes (refactored)

include/scene/
├── SceneSerializer.h       - Scene save/load API
├── SceneFile.h             - .miscene file format definitions
└── ActorSpawner.h          - Helper for spawning actors

src/core/
├── MiObject.cpp            - UUID generation, base serialization
├── MiActor.cpp             - Component management, transform
├── MiComponent.cpp         - Component lifecycle
├── MiSceneComponent.cpp    - Hierarchical transforms
├── MiWorld.cpp             - Actor management, update loop
├── MiObjectFactory.cpp     - Factory pattern implementation
└── MiTypeRegistry.cpp      - Type registration

src/actor/
├── MiStaticMeshActor.cpp
├── MiSkeletalMeshActor.cpp
├── MiLightActor.cpp
├── MiCameraActor.cpp
└── MiEmptyActor.cpp

src/component/
├── MiStaticMeshComponent.cpp
├── MiSkeletalMeshComponent.cpp
├── MiLightComponent.cpp
├── MiCameraComponent.cpp
├── MiRigidBodyComponent.cpp
└── MiColliderComponent.cpp

src/scene/
├── SceneSerializer.cpp     - JSON serialization implementation
├── SceneFile.cpp           - File I/O helpers
└── ActorSpawner.cpp        - Actor creation helpers
```

---

## Core Classes

### MiObject (Base Class)

```cpp
// include/core/MiObject.h
#pragma once
#include <string>
#include <cstdint>
#include <memory>

namespace MiEngine {

// Unique identifier type (128-bit UUID as string)
using ObjectId = std::string;

// Generate a new unique ID
ObjectId generateObjectId();

// Forward declarations
class MiWorld;

// Base class for all engine objects
class MiObject : public std::enable_shared_from_this<MiObject> {
public:
    MiObject();
    virtual ~MiObject() = default;

    // Unique identifier
    const ObjectId& getObjectId() const { return m_ObjectId; }

    // Display name
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }

    // Runtime type info
    virtual const char* getTypeName() const = 0;
    virtual uint32_t getTypeId() const = 0;

    // Serialization
    virtual void serialize(class JsonWriter& writer) const;
    virtual void deserialize(const class JsonReader& reader);

    // Lifecycle
    virtual void onCreated() {}      // Called after construction
    virtual void onDestroyed() {}    // Called before destruction

    // Flags
    bool isPendingDestroy() const { return m_PendingDestroy; }
    void markPendingDestroy() { m_PendingDestroy = true; }

protected:
    ObjectId m_ObjectId;
    std::string m_Name = "Object";
    bool m_PendingDestroy = false;
};

// Macro for declaring type info
#define MI_OBJECT_BODY(TypeName, TypeIdValue) \
    public: \
        static constexpr const char* StaticTypeName = #TypeName; \
        static constexpr uint32_t StaticTypeId = TypeIdValue; \
        virtual const char* getTypeName() const override { return StaticTypeName; } \
        virtual uint32_t getTypeId() const override { return StaticTypeId; }

} // namespace MiEngine
```

### MiActor (Actor Base Class)

```cpp
// include/core/MiActor.h
#pragma once
#include "MiObject.h"
#include "MiSceneComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>

namespace MiEngine {

class MiComponent;
class MiWorld;

// Transform structure (similar to existing but with quaternion rotation)
struct MiTransform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 getMatrix() const;
    void setFromMatrix(const glm::mat4& matrix);

    // Euler angles helpers (radians)
    glm::vec3 getEulerAngles() const;
    void setEulerAngles(const glm::vec3& eulerRadians);

    // Direction vectors
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    // Serialization
    void serialize(class JsonWriter& writer) const;
    void deserialize(const class JsonReader& reader);
};

// Actor flags
enum class ActorFlags : uint32_t {
    None = 0,
    Hidden = 1 << 0,           // Don't render
    Transient = 1 << 1,        // Don't save to scene
    EditorOnly = 1 << 2,       // Only exists in editor
    Static = 1 << 3,           // Won't move at runtime (optimization)
    Selected = 1 << 4,         // Currently selected in editor
};

inline ActorFlags operator|(ActorFlags a, ActorFlags b) {
    return static_cast<ActorFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasFlag(ActorFlags flags, ActorFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Base class for all actors
class MiActor : public MiObject {
    MI_OBJECT_BODY(MiActor, 100)

public:
    MiActor();
    virtual ~MiActor();

    // World ownership
    MiWorld* getWorld() const { return m_World; }
    void setWorld(MiWorld* world) { m_World = world; }

    // Transform (root component transform)
    const MiTransform& getTransform() const;
    MiTransform& getTransform();
    void setTransform(const MiTransform& transform);

    // Convenience accessors
    glm::vec3 getPosition() const { return getTransform().position; }
    glm::quat getRotation() const { return getTransform().rotation; }
    glm::vec3 getScale() const { return getTransform().scale; }
    void setPosition(const glm::vec3& pos);
    void setRotation(const glm::quat& rot);
    void setScale(const glm::vec3& scale);

    // Component management
    template<typename T, typename... Args>
    std::shared_ptr<T> addComponent(Args&&... args);

    template<typename T>
    std::shared_ptr<T> getComponent() const;

    template<typename T>
    std::vector<std::shared_ptr<T>> getComponents() const;

    void removeComponent(std::shared_ptr<MiComponent> component);
    const std::vector<std::shared_ptr<MiComponent>>& getAllComponents() const { return m_Components; }

    // Root scene component (defines actor's transform)
    std::shared_ptr<MiSceneComponent> getRootComponent() const { return m_RootComponent; }
    void setRootComponent(std::shared_ptr<MiSceneComponent> root);

    // Lifecycle
    virtual void beginPlay() {}      // Called when actor starts playing
    virtual void endPlay() {}        // Called when actor stops playing
    virtual void tick(float deltaTime) {}  // Called every frame

    // Flags
    ActorFlags getFlags() const { return m_Flags; }
    void setFlags(ActorFlags flags) { m_Flags = flags; }
    void addFlags(ActorFlags flags) { m_Flags = m_Flags | flags; }
    bool isHidden() const { return hasFlag(m_Flags, ActorFlags::Hidden); }
    bool isTransient() const { return hasFlag(m_Flags, ActorFlags::Transient); }
    bool isStatic() const { return hasFlag(m_Flags, ActorFlags::Static); }

    // Tags for filtering
    void addTag(const std::string& tag) { m_Tags.push_back(tag); }
    void removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;
    const std::vector<std::string>& getTags() const { return m_Tags; }

    // Layer (for rendering/physics grouping)
    uint32_t getLayer() const { return m_Layer; }
    void setLayer(uint32_t layer) { m_Layer = layer; }

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    // Called when a component is added
    virtual void onComponentAdded(std::shared_ptr<MiComponent> component) {}
    virtual void onComponentRemoved(std::shared_ptr<MiComponent> component) {}

private:
    MiWorld* m_World = nullptr;
    std::shared_ptr<MiSceneComponent> m_RootComponent;
    std::vector<std::shared_ptr<MiComponent>> m_Components;
    std::unordered_map<std::type_index, std::vector<size_t>> m_ComponentsByType;
    ActorFlags m_Flags = ActorFlags::None;
    std::vector<std::string> m_Tags;
    uint32_t m_Layer = 0;
};

// Template implementations
template<typename T, typename... Args>
std::shared_ptr<T> MiActor::addComponent(Args&&... args) {
    static_assert(std::is_base_of<MiComponent, T>::value, "T must derive from MiComponent");

    auto component = std::make_shared<T>(std::forward<Args>(args)...);
    component->setOwner(this);

    m_Components.push_back(component);
    m_ComponentsByType[typeid(T)].push_back(m_Components.size() - 1);

    component->onAttached();
    onComponentAdded(component);

    return component;
}

template<typename T>
std::shared_ptr<T> MiActor::getComponent() const {
    auto it = m_ComponentsByType.find(typeid(T));
    if (it != m_ComponentsByType.end() && !it->second.empty()) {
        return std::static_pointer_cast<T>(m_Components[it->second[0]]);
    }

    // Fallback: search all components for derived types
    for (const auto& comp : m_Components) {
        if (auto cast = std::dynamic_pointer_cast<T>(comp)) {
            return cast;
        }
    }
    return nullptr;
}

template<typename T>
std::vector<std::shared_ptr<T>> MiActor::getComponents() const {
    std::vector<std::shared_ptr<T>> result;
    for (const auto& comp : m_Components) {
        if (auto cast = std::dynamic_pointer_cast<T>(comp)) {
            result.push_back(cast);
        }
    }
    return result;
}

} // namespace MiEngine
```

### MiComponent (Component Base Class)

```cpp
// include/core/MiComponent.h
#pragma once
#include "MiObject.h"

namespace MiEngine {

class MiActor;

// Base class for all components
class MiComponent : public MiObject {
    MI_OBJECT_BODY(MiComponent, 200)

public:
    MiComponent() = default;
    virtual ~MiComponent() = default;

    // Owner actor
    MiActor* getOwner() const { return m_Owner; }
    void setOwner(MiActor* owner) { m_Owner = owner; }

    // Enable/disable
    bool isEnabled() const { return m_Enabled; }
    void setEnabled(bool enabled);

    // Lifecycle (called by actor)
    virtual void onAttached() {}     // Called when added to actor
    virtual void onDetached() {}     // Called when removed from actor
    virtual void beginPlay() {}      // Called when game starts
    virtual void endPlay() {}        // Called when game ends
    virtual void tick(float deltaTime) {}  // Called every frame if tickable

    // Does this component need tick?
    virtual bool isTickable() const { return false; }

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    virtual void onEnabledChanged(bool enabled) {}

private:
    MiActor* m_Owner = nullptr;
    bool m_Enabled = true;
};

} // namespace MiEngine
```

### MiSceneComponent (Transform Component)

```cpp
// include/core/MiSceneComponent.h
#pragma once
#include "MiComponent.h"
#include "MiActor.h"  // For MiTransform
#include <vector>

namespace MiEngine {

// Component with a transform (can have parent/children)
class MiSceneComponent : public MiComponent {
    MI_OBJECT_BODY(MiSceneComponent, 201)

public:
    MiSceneComponent() = default;
    virtual ~MiSceneComponent() = default;

    // Local transform (relative to parent)
    const MiTransform& getLocalTransform() const { return m_LocalTransform; }
    void setLocalTransform(const MiTransform& transform);

    glm::vec3 getLocalPosition() const { return m_LocalTransform.position; }
    glm::quat getLocalRotation() const { return m_LocalTransform.rotation; }
    glm::vec3 getLocalScale() const { return m_LocalTransform.scale; }
    void setLocalPosition(const glm::vec3& pos);
    void setLocalRotation(const glm::quat& rot);
    void setLocalScale(const glm::vec3& scale);

    // World transform (computed from hierarchy)
    MiTransform getWorldTransform() const;
    glm::mat4 getWorldMatrix() const;
    glm::vec3 getWorldPosition() const;
    glm::quat getWorldRotation() const;
    glm::vec3 getWorldScale() const;
    void setWorldPosition(const glm::vec3& pos);
    void setWorldRotation(const glm::quat& rot);

    // Hierarchy
    MiSceneComponent* getParent() const { return m_Parent; }
    void setParent(MiSceneComponent* parent);
    void attachTo(std::shared_ptr<MiSceneComponent> parent);
    void detachFromParent();

    const std::vector<std::shared_ptr<MiSceneComponent>>& getChildren() const { return m_Children; }
    void addChild(std::shared_ptr<MiSceneComponent> child);
    void removeChild(std::shared_ptr<MiSceneComponent> child);

    // Visibility
    bool isVisible() const { return m_Visible; }
    void setVisible(bool visible) { m_Visible = visible; }

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    // Called when transform changes
    virtual void onTransformChanged() {}
    void markTransformDirty();

private:
    void updateWorldTransform() const;

    MiTransform m_LocalTransform;
    mutable MiTransform m_CachedWorldTransform;
    mutable bool m_WorldTransformDirty = true;

    MiSceneComponent* m_Parent = nullptr;
    std::vector<std::shared_ptr<MiSceneComponent>> m_Children;
    bool m_Visible = true;
};

} // namespace MiEngine
```

### MiWorld (World Container)

```cpp
// include/core/MiWorld.h
#pragma once
#include "MiObject.h"
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace MiEngine {

class MiActor;
class PhysicsWorld;
class VulkanRenderer;

// World settings
struct WorldSettings {
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float physicsTimeStep = 1.0f / 60.0f;
    bool enablePhysics = true;
};

// Main world class containing all actors
class MiWorld : public MiObject {
    MI_OBJECT_BODY(MiWorld, 50)

public:
    MiWorld();
    ~MiWorld();

    // Initialization
    void initialize(VulkanRenderer* renderer);
    void shutdown();

    // Actor management
    template<typename T, typename... Args>
    std::shared_ptr<T> spawnActor(Args&&... args);

    std::shared_ptr<MiActor> spawnActorByType(const std::string& typeName);
    void destroyActor(std::shared_ptr<MiActor> actor);
    void destroyActor(const ObjectId& id);
    void destroyAllActors();

    // Actor queries
    std::shared_ptr<MiActor> findActorById(const ObjectId& id) const;
    std::shared_ptr<MiActor> findActorByName(const std::string& name) const;
    std::vector<std::shared_ptr<MiActor>> findActorsByTag(const std::string& tag) const;
    std::vector<std::shared_ptr<MiActor>> findActorsByLayer(uint32_t layer) const;

    template<typename T>
    std::vector<std::shared_ptr<T>> findActorsOfType() const;

    const std::vector<std::shared_ptr<MiActor>>& getAllActors() const { return m_Actors; }
    size_t getActorCount() const { return m_Actors.size(); }

    // Iteration
    void forEachActor(const std::function<void(MiActor*)>& callback);
    void forEachActor(const std::function<void(const MiActor*)>& callback) const;

    // Update loop
    void tick(float deltaTime);
    void beginPlay();
    void endPlay();

    // Physics
    PhysicsWorld* getPhysicsWorld() const { return m_PhysicsWorld.get(); }
    const WorldSettings& getSettings() const { return m_Settings; }
    void setSettings(const WorldSettings& settings) { m_Settings = settings; }

    // Renderer access
    VulkanRenderer* getRenderer() const { return m_Renderer; }

    // Dirty flag for scene saving
    bool isDirty() const { return m_Dirty; }
    void markDirty() { m_Dirty = true; }
    void clearDirty() { m_Dirty = false; }

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    void processDestroyQueue();
    void registerActor(std::shared_ptr<MiActor> actor);
    void unregisterActor(std::shared_ptr<MiActor> actor);

    VulkanRenderer* m_Renderer = nullptr;
    std::unique_ptr<PhysicsWorld> m_PhysicsWorld;
    std::vector<std::shared_ptr<MiActor>> m_Actors;
    std::unordered_map<ObjectId, std::shared_ptr<MiActor>> m_ActorMap;
    std::vector<std::shared_ptr<MiActor>> m_DestroyQueue;
    WorldSettings m_Settings;
    bool m_Dirty = false;
    bool m_IsPlaying = false;
};

// Template implementations
template<typename T, typename... Args>
std::shared_ptr<T> MiWorld::spawnActor(Args&&... args) {
    static_assert(std::is_base_of<MiActor, T>::value, "T must derive from MiActor");

    auto actor = std::make_shared<T>(std::forward<Args>(args)...);
    registerActor(actor);

    actor->onCreated();
    if (m_IsPlaying) {
        actor->beginPlay();
    }

    markDirty();
    return actor;
}

template<typename T>
std::vector<std::shared_ptr<T>> MiWorld::findActorsOfType() const {
    std::vector<std::shared_ptr<T>> result;
    for (const auto& actor : m_Actors) {
        if (auto cast = std::dynamic_pointer_cast<T>(actor)) {
            result.push_back(cast);
        }
    }
    return result;
}

} // namespace MiEngine
```

---

## Actor Types

### MiStaticMeshActor

```cpp
// include/actor/MiStaticMeshActor.h
#pragma once
#include "core/MiActor.h"
#include "component/MiStaticMeshComponent.h"

namespace MiEngine {

// Actor containing a static mesh
class MiStaticMeshActor : public MiActor {
    MI_OBJECT_BODY(MiStaticMeshActor, 101)

public:
    MiStaticMeshActor();

    // Mesh component access
    std::shared_ptr<MiStaticMeshComponent> getMeshComponent() const { return m_MeshComponent; }

    // Quick setup
    void setMesh(const std::string& assetPath);
    void setMesh(std::shared_ptr<Mesh> mesh);
    void setMaterial(const Material& material);

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    std::shared_ptr<MiStaticMeshComponent> m_MeshComponent;
};

} // namespace MiEngine
```

### MiSkeletalMeshActor

```cpp
// include/actor/MiSkeletalMeshActor.h
#pragma once
#include "core/MiActor.h"
#include "component/MiSkeletalMeshComponent.h"

namespace MiEngine {

class AnimationClip;

// Actor containing a skeletal mesh with animations
class MiSkeletalMeshActor : public MiActor {
    MI_OBJECT_BODY(MiSkeletalMeshActor, 102)

public:
    MiSkeletalMeshActor();

    // Skeletal mesh component access
    std::shared_ptr<MiSkeletalMeshComponent> getMeshComponent() const { return m_MeshComponent; }

    // Animation control
    void playAnimation(const std::string& animName, bool loop = true);
    void playAnimation(size_t animIndex, bool loop = true);
    void stopAnimation();
    void setPlaybackSpeed(float speed);
    float getPlaybackSpeed() const;

    // Quick setup
    void setSkeletalMesh(const std::string& assetPath);

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    std::shared_ptr<MiSkeletalMeshComponent> m_MeshComponent;
};

} // namespace MiEngine
```

### MiLightActor

```cpp
// include/actor/MiLightActor.h
#pragma once
#include "core/MiActor.h"
#include "component/MiLightComponent.h"

namespace MiEngine {

// Light type enumeration
enum class LightType : uint8_t {
    Point = 0,
    Directional = 1,
    Spot = 2
};

// Actor containing a light source
class MiLightActor : public MiActor {
    MI_OBJECT_BODY(MiLightActor, 103)

public:
    MiLightActor(LightType type = LightType::Point);

    // Light component access
    std::shared_ptr<MiLightComponent> getLightComponent() const { return m_LightComponent; }

    // Light properties
    LightType getLightType() const;

    glm::vec3 getColor() const;
    void setColor(const glm::vec3& color);

    float getIntensity() const;
    void setIntensity(float intensity);

    float getRadius() const;
    void setRadius(float radius);

    bool castsShadows() const;
    void setCastsShadows(bool casts);

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    std::shared_ptr<MiLightComponent> m_LightComponent;
    LightType m_LightType;
};

} // namespace MiEngine
```

### MiCameraActor

```cpp
// include/actor/MiCameraActor.h
#pragma once
#include "core/MiActor.h"
#include "component/MiCameraComponent.h"

namespace MiEngine {

// Actor containing a camera
class MiCameraActor : public MiActor {
    MI_OBJECT_BODY(MiCameraActor, 104)

public:
    MiCameraActor();

    // Camera component access
    std::shared_ptr<MiCameraComponent> getCameraComponent() const { return m_CameraComponent; }

    // Camera properties
    float getFOV() const;
    void setFOV(float fov);

    float getNearPlane() const;
    float getFarPlane() const;
    void setClipPlanes(float nearPlane, float farPlane);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Make this the active camera
    void setActive(bool active);
    bool isActive() const;

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    std::shared_ptr<MiCameraComponent> m_CameraComponent;
};

} // namespace MiEngine
```

### MiEmptyActor

```cpp
// include/actor/MiEmptyActor.h
#pragma once
#include "core/MiActor.h"

namespace MiEngine {

// Empty actor for organization/grouping (like an empty GameObject in Unity)
class MiEmptyActor : public MiActor {
    MI_OBJECT_BODY(MiEmptyActor, 105)

public:
    MiEmptyActor() = default;

    // No additional functionality - just a transform holder
};

} // namespace MiEngine
```

---

## Scene Serialization

### Scene File Format (.miscene)

```json
{
  "version": 1,
  "name": "MainLevel",
  "description": "Main game level",
  "settings": {
    "gravity": [0.0, -9.81, 0.0],
    "physicsTimeStep": 0.016667,
    "enablePhysics": true,
    "ambientColor": [0.1, 0.1, 0.1],
    "skybox": "Assets/HDR/environment.hdr"
  },
  "actors": [
    {
      "type": "MiStaticMeshActor",
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Floor",
      "layer": 0,
      "flags": 8,
      "tags": ["environment", "static"],
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation": [1.0, 0.0, 0.0, 0.0],
        "scale": [10.0, 1.0, 10.0]
      },
      "components": [
        {
          "type": "MiStaticMeshComponent",
          "id": "550e8400-e29b-41d4-a716-446655440001",
          "name": "MeshComponent",
          "enabled": true,
          "visible": true,
          "meshAsset": "Assets/Models/cube.fbx",
          "material": {
            "albedo": "Assets/Textures/floor_albedo.png",
            "normal": "Assets/Textures/floor_normal.png",
            "metallic": 0.0,
            "roughness": 0.8
          }
        }
      ]
    },
    {
      "type": "MiSkeletalMeshActor",
      "id": "550e8400-e29b-41d4-a716-446655440002",
      "name": "Character",
      "transform": {
        "position": [0.0, 1.0, 0.0],
        "rotation": [1.0, 0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "components": [
        {
          "type": "MiSkeletalMeshComponent",
          "meshAsset": "Assets/Models/character.fbx",
          "currentAnimation": "Idle",
          "playbackSpeed": 1.0,
          "looping": true
        },
        {
          "type": "MiRigidBodyComponent",
          "bodyType": "Dynamic",
          "mass": 70.0,
          "useGravity": true,
          "lockRotationX": true,
          "lockRotationZ": true
        },
        {
          "type": "MiColliderComponent",
          "shape": "Capsule",
          "radius": 0.5,
          "height": 1.8
        }
      ]
    },
    {
      "type": "MiLightActor",
      "id": "550e8400-e29b-41d4-a716-446655440003",
      "name": "SunLight",
      "lightType": "Directional",
      "transform": {
        "position": [0.0, 100.0, 0.0],
        "rotation": [0.707, -0.707, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "components": [
        {
          "type": "MiDirectionalLightComponent",
          "color": [1.0, 0.95, 0.9],
          "intensity": 1.5,
          "castsShadows": true,
          "shadowBias": 0.001
        }
      ]
    },
    {
      "type": "MiCameraActor",
      "id": "550e8400-e29b-41d4-a716-446655440004",
      "name": "MainCamera",
      "transform": {
        "position": [0.0, 5.0, -10.0],
        "rotation": [0.996, 0.087, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "components": [
        {
          "type": "MiCameraComponent",
          "fov": 60.0,
          "nearPlane": 0.1,
          "farPlane": 1000.0,
          "isActive": true
        }
      ]
    }
  ]
}
```

### SceneSerializer API

```cpp
// include/scene/SceneSerializer.h
#pragma once
#include <string>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace MiEngine {

class MiWorld;

// Scene file metadata
struct SceneMetadata {
    std::string name;
    std::string description;
    std::string author;
    uint32_t version = 1;
    uint64_t createdTime = 0;
    uint64_t modifiedTime = 0;
};

// Result of scene operations
struct SceneResult {
    bool success = false;
    std::string errorMessage;
    SceneMetadata metadata;
};

// Scene serialization/deserialization
class SceneSerializer {
public:
    // Save world to .miscene file
    static SceneResult saveScene(const MiWorld& world, const fs::path& filePath);
    static SceneResult saveScene(const MiWorld& world, const fs::path& filePath, const SceneMetadata& metadata);

    // Load world from .miscene file
    static SceneResult loadScene(MiWorld& world, const fs::path& filePath);

    // Get metadata without loading full scene
    static SceneResult peekScene(const fs::path& filePath);

    // Validate scene file
    static bool validateScene(const fs::path& filePath);

    // Create empty scene with default actors
    static void createDefaultScene(MiWorld& world);

private:
    // Internal serialization
    static void serializeActor(const class MiActor* actor, class JsonWriter& writer);
    static std::shared_ptr<MiActor> deserializeActor(MiWorld& world, const class JsonReader& reader);

    static void serializeComponent(const class MiComponent* component, class JsonWriter& writer);
    static std::shared_ptr<MiComponent> deserializeComponent(const class JsonReader& reader);
};

} // namespace MiEngine
```

---

## Type Registry and Factory

### MiTypeRegistry

```cpp
// include/core/MiTypeRegistry.h
#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

namespace MiEngine {

class MiObject;

// Factory function type
using ObjectFactory = std::function<std::shared_ptr<MiObject>()>;

// Runtime type registration
class MiTypeRegistry {
public:
    static MiTypeRegistry& getInstance();

    // Register a type
    template<typename T>
    void registerType();

    // Create object by type name
    std::shared_ptr<MiObject> create(const std::string& typeName) const;

    // Check if type is registered
    bool isRegistered(const std::string& typeName) const;

    // Get all registered type names
    std::vector<std::string> getRegisteredTypes() const;

private:
    MiTypeRegistry() = default;
    std::unordered_map<std::string, ObjectFactory> m_Factories;
};

// Template implementation
template<typename T>
void MiTypeRegistry::registerType() {
    m_Factories[T::StaticTypeName] = []() -> std::shared_ptr<MiObject> {
        return std::make_shared<T>();
    };
}

// Registration macro
#define MI_REGISTER_TYPE(TypeName) \
    namespace { \
        struct TypeName##_Registrar { \
            TypeName##_Registrar() { \
                MiEngine::MiTypeRegistry::getInstance().registerType<TypeName>(); \
            } \
        }; \
        static TypeName##_Registrar s_##TypeName##_Registrar; \
    }

} // namespace MiEngine
```

---

## JSON Helpers

### JsonWriter and JsonReader

```cpp
// include/core/JsonIO.h
#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <fstream>
#include <sstream>

namespace MiEngine {

// JSON writer for serialization
class JsonWriter {
public:
    JsonWriter();

    void beginObject();
    void endObject();
    void beginArray(const std::string& key);
    void endArray();

    void writeKey(const std::string& key);
    void writeString(const std::string& key, const std::string& value);
    void writeInt(const std::string& key, int value);
    void writeUInt(const std::string& key, uint32_t value);
    void writeUInt64(const std::string& key, uint64_t value);
    void writeFloat(const std::string& key, float value);
    void writeBool(const std::string& key, bool value);
    void writeVec3(const std::string& key, const glm::vec3& value);
    void writeVec4(const std::string& key, const glm::vec4& value);
    void writeQuat(const std::string& key, const glm::quat& value);

    std::string toString() const;
    bool saveToFile(const std::string& filePath) const;

private:
    std::ostringstream m_Stream;
    int m_Indent = 0;
    bool m_NeedComma = false;
    std::vector<bool> m_ArrayStack;

    void writeIndent();
    void writeCommaIfNeeded();
};

// JSON reader for deserialization
class JsonReader {
public:
    JsonReader();

    bool loadFromFile(const std::string& filePath);
    bool loadFromString(const std::string& json);

    bool hasKey(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    uint32_t getUInt(const std::string& key, uint32_t defaultValue = 0) const;
    uint64_t getUInt64(const std::string& key, uint64_t defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    glm::vec3 getVec3(const std::string& key, const glm::vec3& defaultValue = glm::vec3(0.0f)) const;
    glm::vec4 getVec4(const std::string& key, const glm::vec4& defaultValue = glm::vec4(0.0f)) const;
    glm::quat getQuat(const std::string& key, const glm::quat& defaultValue = glm::quat(1, 0, 0, 0)) const;

    // Array access
    std::vector<JsonReader> getArray(const std::string& key) const;
    JsonReader getObject(const std::string& key) const;

    // Raw JSON string for this object/array
    const std::string& getRawJson() const { return m_Json; }

private:
    std::string m_Json;

    std::string extractValue(const std::string& key) const;
    std::string extractArrayElement(size_t index) const;
};

} // namespace MiEngine
```

---

## Migration Guide

### Converting Existing MeshInstance to Actor

**Before (Current System):**
```cpp
// Load model directly into scene
m_Scene->loadModel("character.fbx", transform);
auto* instance = m_Scene->getMeshInstance(0);
instance->rigidBody = std::make_shared<RigidBodyComponent>();
instance->collider = std::make_shared<ColliderComponent>();
```

**After (Actor System):**
```cpp
// Create actor with components
auto character = m_World->spawnActor<MiStaticMeshActor>();
character->setName("Character");
character->setTransform(transform);
character->setMesh("character.fbx");

// Add physics components
auto rigidBody = character->addComponent<MiRigidBodyComponent>();
rigidBody->setMass(70.0f);

auto collider = character->addComponent<MiColliderComponent>();
collider->setShape(ColliderShape::Capsule);
```

### Scene Saving/Loading

```cpp
// Save scene
auto& world = m_Renderer->getWorld();
SceneMetadata metadata;
metadata.name = "MyLevel";
metadata.description = "Main game level";

auto result = SceneSerializer::saveScene(world, project->getScenesPath() / "MyLevel.miscene", metadata);
if (!result.success) {
    std::cerr << "Failed to save: " << result.errorMessage << std::endl;
}

// Load scene
MiWorld newWorld;
newWorld.initialize(m_Renderer);
auto loadResult = SceneSerializer::loadScene(newWorld, "Scenes/MyLevel.miscene");
if (loadResult.success) {
    std::cout << "Loaded scene: " << loadResult.metadata.name << std::endl;
}
```

---

## Integration with Existing Systems

### Scene.h/cpp Compatibility

The existing `Scene` class will be refactored to use `MiWorld` internally:

```cpp
class Scene {
public:
    // Existing API maintained for backwards compatibility
    bool loadModel(const std::string& filename, const Transform& transform = Transform());

    // New: Returns the underlying world
    MiWorld& getWorld() { return *m_World; }

    // Converts legacy MeshInstance to actor
    std::shared_ptr<MiActor> convertToActor(size_t meshInstanceIndex);

private:
    std::unique_ptr<MiWorld> m_World;

    // Legacy support during transition
    std::vector<MeshInstance> meshInstances;  // Will be deprecated
};
```

### Renderer Integration

```cpp
class VulkanRenderer {
public:
    // New world-based rendering
    void renderWorld(const MiWorld& world, VkCommandBuffer cmd);

private:
    // Collects visible actors and components for rendering
    void collectRenderables(const MiWorld& world);
};
```

---

## Debug UI Integration

### World Outliner Panel

New debug panel showing actor hierarchy:

```cpp
// include/debug/WorldOutlinerPanel.h
class WorldOutlinerPanel : public DebugPanel {
public:
    void render() override;

private:
    void renderActorNode(MiActor* actor);
    void renderComponentList(MiActor* actor);

    MiActor* m_SelectedActor = nullptr;
};
```

### Actor Details Panel

Panel showing selected actor's properties and components:

```cpp
// include/debug/ActorDetailsPanel.h
class ActorDetailsPanel : public DebugPanel {
public:
    void render() override;
    void setSelectedActor(MiActor* actor);

private:
    void renderTransformEditor();
    void renderComponentEditor(MiComponent* component);

    MiActor* m_SelectedActor = nullptr;
};
```

---

## Type ID Assignments

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
| MiSkeletalMeshComponent | 211 |
| MiPointLightComponent | 220 |
| MiDirectionalLightComponent | 221 |
| MiSpotLightComponent | 222 |
| MiCameraComponent | 230 |
| MiRigidBodyComponent | 240 |
| MiColliderComponent | 241 |

---

## Implementation Order

1. **Phase 1: Core Classes**
   - MiObject, MiTypeRegistry, JsonWriter/JsonReader
   - MiComponent, MiSceneComponent
   - MiActor base class
   - MiWorld container

2. **Phase 2: Actor Types**
   - MiEmptyActor
   - MiStaticMeshActor + MiStaticMeshComponent
   - MiLightActor + MiLightComponent variants

3. **Phase 3: Scene Serialization**
   - SceneSerializer implementation
   - .miscene file format
   - Load/save functionality

4. **Phase 4: Skeletal & Physics**
   - MiSkeletalMeshActor + MiSkeletalMeshComponent
   - MiRigidBodyComponent refactor
   - MiColliderComponent refactor

5. **Phase 5: Debug UI**
   - WorldOutlinerPanel
   - ActorDetailsPanel
   - Scene save/load in menu

6. **Phase 6: Migration**
   - Scene class integration
   - Backwards compatibility layer
   - Legacy code deprecation

---

## Scalability Design

This architecture is designed to scale with your project. Here's how to extend it:

### Adding New Actor Types

```cpp
// 1. Create header: include/actor/MiVehicleActor.h
#pragma once
#include "core/MiActor.h"

namespace MiEngine {

class MiVehicleActor : public MiActor {
    MI_OBJECT_BODY(MiVehicleActor, 110)  // Pick next available ID

public:
    MiVehicleActor();

    // Vehicle-specific API
    void accelerate(float amount);
    void steer(float angle);
    float getSpeed() const;

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    float m_CurrentSpeed = 0.0f;
    float m_MaxSpeed = 100.0f;
};

} // namespace MiEngine

// 2. In source file, register the type:
// src/actor/MiVehicleActor.cpp
#include "actor/MiVehicleActor.h"
MI_REGISTER_TYPE(MiVehicleActor)  // Auto-registers for serialization

// Now it works automatically:
auto car = world->spawnActor<MiVehicleActor>();
// And serialization just works - saved scenes will include the type name
```

### Adding New Component Types

```cpp
// 1. Create: include/component/MiHealthComponent.h
class MiHealthComponent : public MiComponent {
    MI_OBJECT_BODY(MiHealthComponent, 250)

public:
    float getHealth() const { return m_Health; }
    void setHealth(float health);
    void takeDamage(float damage);
    bool isDead() const { return m_Health <= 0; }

    // Events (delegate pattern)
    std::function<void(float, float)> onHealthChanged;  // old, new
    std::function<void()> onDeath;

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    float m_Health = 100.0f;
    float m_MaxHealth = 100.0f;
};

// 2. Use on any actor:
auto enemy = world->spawnActor<MiSkeletalMeshActor>();
auto health = enemy->addComponent<MiHealthComponent>();
health->onDeath = [enemy]() {
    enemy->playAnimation("Death");
};
```

### Creating Actor Hierarchies (Like UE5)

```cpp
// Intermediate actor classes
class MiPawn : public MiActor {
    MI_OBJECT_BODY(MiPawn, 120)
public:
    virtual void setupPlayerInput(class InputComponent* input) {}
    void possess(class MiController* controller);
protected:
    MiController* m_Controller = nullptr;
};

class MiCharacter : public MiPawn {
    MI_OBJECT_BODY(MiCharacter, 121)
public:
    void jump();
    void move(const glm::vec3& direction);
protected:
    std::shared_ptr<MiCapsuleColliderComponent> m_CapsuleCollider;
    std::shared_ptr<MiCharacterMovementComponent> m_MovementComponent;
};

// Game-specific
class MyPlayerCharacter : public MiCharacter {
    MI_OBJECT_BODY(MyPlayerCharacter, 1000)  // Game types start at 1000
public:
    void setupPlayerInput(InputComponent* input) override;
    void attack();
private:
    int m_ComboCount = 0;
};
```

### Property System (Future-Proof Serialization)

```cpp
// Optional: Add a property system for automatic serialization
#define MI_PROPERTY(Type, Name, DefaultValue) \
    private: Type m_##Name = DefaultValue; \
    public: \
        Type get##Name() const { return m_##Name; } \
        void set##Name(Type value) { m_##Name = value; markDirty(); }

class MiHealthComponent : public MiComponent {
    MI_OBJECT_BODY(MiHealthComponent, 250)

    MI_PROPERTY(float, Health, 100.0f)
    MI_PROPERTY(float, MaxHealth, 100.0f)
    MI_PROPERTY(float, RegenRate, 0.0f)
    MI_PROPERTY(bool, Invulnerable, false)

    // Properties auto-serialize if you implement property iteration
};
```

### Interface System (Component Queries)

```cpp
// Define interfaces for cross-cutting concerns
class IDamageable {
public:
    virtual ~IDamageable() = default;
    virtual void takeDamage(float amount, MiActor* instigator) = 0;
    virtual float getHealth() const = 0;
};

class IInteractable {
public:
    virtual ~IInteractable() = default;
    virtual void interact(MiActor* interactor) = 0;
    virtual std::string getInteractionPrompt() const = 0;
};

// Components implement interfaces
class MiHealthComponent : public MiComponent, public IDamageable {
    void takeDamage(float amount, MiActor* instigator) override;
    float getHealth() const override { return m_Health; }
};

// Query by interface
template<typename TInterface>
TInterface* MiActor::getInterface() {
    for (auto& comp : m_Components) {
        if (auto* iface = dynamic_cast<TInterface*>(comp.get())) {
            return iface;
        }
    }
    return nullptr;
}

// Usage
if (auto* damageable = hitActor->getInterface<IDamageable>()) {
    damageable->takeDamage(10.0f, shooter);
}
```

### Event/Delegate System

```cpp
// include/core/MiDelegate.h
template<typename... Args>
class MiDelegate {
public:
    using FunctionType = std::function<void(Args...)>;

    void bind(FunctionType func) { m_Callbacks.push_back(func); }
    void broadcast(Args... args) {
        for (auto& callback : m_Callbacks) {
            callback(args...);
        }
    }
    void clear() { m_Callbacks.clear(); }

private:
    std::vector<FunctionType> m_Callbacks;
};

// Usage in components
class MiHealthComponent : public MiComponent {
public:
    MiDelegate<float, float> OnHealthChanged;  // oldHealth, newHealth
    MiDelegate<MiActor*> OnDeath;              // killer

    void takeDamage(float damage, MiActor* instigator) {
        float oldHealth = m_Health;
        m_Health = std::max(0.0f, m_Health - damage);
        OnHealthChanged.broadcast(oldHealth, m_Health);

        if (m_Health <= 0) {
            OnDeath.broadcast(instigator);
        }
    }
};
```

### Subsystem Pattern (Global Services)

```cpp
// For systems that don't belong to specific actors
class MiWorldSubsystem {
public:
    virtual ~MiWorldSubsystem() = default;
    virtual void initialize(MiWorld* world) {}
    virtual void shutdown() {}
    virtual void tick(float deltaTime) {}
};

class MiAudioSubsystem : public MiWorldSubsystem {
public:
    void playSound(const std::string& sound, const glm::vec3& location);
    void playMusic(const std::string& track);
};

class MiAISubsystem : public MiWorldSubsystem {
public:
    void registerAIController(class MiAIController* controller);
    void tick(float deltaTime) override;  // Updates all AI
};

// Access from world
auto* audio = world->getSubsystem<MiAudioSubsystem>();
audio->playSound("explosion.wav", explosionPos);
```

### Type ID Ranges (Organized Growth)

| Range | Category |
|-------|----------|
| 1-49 | Reserved/System |
| 50-99 | World/Scene types |
| 100-199 | Actor base types |
| 200-299 | Component base types |
| 300-399 | Physics components |
| 400-499 | Rendering components |
| 500-599 | Audio components |
| 600-699 | AI components |
| 700-799 | UI components |
| 800-999 | Reserved for engine expansion |
| 1000+ | Game-specific types |

---

## Expanded Class Hierarchy (Full Vision)

```
MiObject
├── MiActor
│   ├── MiEmptyActor
│   ├── MiStaticMeshActor
│   ├── MiSkeletalMeshActor
│   ├── MiLightActor
│   ├── MiCameraActor
│   ├── MiPawn (controllable)
│   │   ├── MiCharacter (humanoid)
│   │   └── MiVehicle (driveable)
│   ├── MiTriggerActor
│   │   ├── MiTriggerBox
│   │   ├── MiTriggerSphere
│   │   └── MiTriggerCapsule
│   ├── MiAudioActor
│   ├── MiDecalActor
│   ├── MiParticleActor
│   ├── MiReflectionProbeActor
│   └── MiNavMeshBoundsActor
│
├── MiComponent
│   ├── MiSceneComponent (has transform)
│   │   ├── MiPrimitiveComponent (renderable)
│   │   │   ├── MiMeshComponent
│   │   │   │   ├── MiStaticMeshComponent
│   │   │   │   └── MiSkeletalMeshComponent
│   │   │   ├── MiShapeComponent
│   │   │   │   ├── MiBoxComponent
│   │   │   │   ├── MiSphereComponent
│   │   │   │   └── MiCapsuleComponent
│   │   │   └── MiDecalComponent
│   │   ├── MiLightComponent
│   │   │   ├── MiPointLightComponent
│   │   │   ├── MiDirectionalLightComponent
│   │   │   └── MiSpotLightComponent
│   │   ├── MiCameraComponent
│   │   ├── MiAudioComponent
│   │   ├── MiParticleSystemComponent
│   │   └── MiSpringArmComponent
│   │
│   └── MiActorComponent (no transform)
│       ├── MiRigidBodyComponent
│       ├── MiColliderComponent
│       ├── MiCharacterMovementComponent
│       ├── MiVehicleMovementComponent
│       ├── MiHealthComponent
│       ├── MiInventoryComponent
│       ├── MiAIControllerComponent
│       ├── MiInputComponent
│       └── MiScriptComponent
│
├── MiController
│   ├── MiPlayerController
│   └── MiAIController
│
└── MiWorldSubsystem
    ├── MiPhysicsSubsystem
    ├── MiAudioSubsystem
    ├── MiNavigationSubsystem
    └── MiAISubsystem
```

---

## TODO (Future Enhancements)

- Actor parenting (attach actors to other actors)
- Prefab system (save/load actor templates)
- Undo/redo for editor operations
- Multi-selection in world outliner
- Copy/paste actors
- Actor blueprints (visual scripting)
- Level streaming (load/unload scene sections)
- Network replication support
- Property reflection system
- Asset references (soft/hard references)
