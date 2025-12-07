#pragma once

#include "core/MiObject.h"
#include "core/MiTransform.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <string>

namespace MiEngine {

// Forward declarations
class MiComponent;
class MiSceneComponent;
class MiWorld;
class JsonWriter;
class JsonReader;

// Actor flags (bitfield)
enum class ActorFlags : uint32_t {
    None        = 0,
    Hidden      = 1 << 0,   // Don't render this actor
    Transient   = 1 << 1,   // Don't save to scene file
    EditorOnly  = 1 << 2,   // Only exists in editor, not in game
    Static      = 1 << 3,   // Won't move at runtime (optimization hint)
    Selected    = 1 << 4,   // Currently selected in editor
    Spawning    = 1 << 5,   // Actor is being spawned
    Destroying  = 1 << 6    // Actor is being destroyed
};

// Bitwise operators for ActorFlags
inline ActorFlags operator|(ActorFlags a, ActorFlags b) {
    return static_cast<ActorFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ActorFlags operator&(ActorFlags a, ActorFlags b) {
    return static_cast<ActorFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ActorFlags& operator|=(ActorFlags& a, ActorFlags b) {
    a = a | b;
    return a;
}

inline ActorFlags& operator&=(ActorFlags& a, ActorFlags b) {
    a = a & b;
    return a;
}

inline bool hasFlag(ActorFlags flags, ActorFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Base class for all actors (similar to AActor in UE5)
// Actors are the primary entities that can be placed in a world
class MiActor : public MiObject {
    MI_OBJECT_BODY(MiActor, 100)

public:
    MiActor();
    virtual ~MiActor();

    // ========================================================================
    // World
    // ========================================================================

    MiWorld* getWorld() const { return m_World; }

    // Note: setWorld is called internally by MiWorld
    void setWorld(MiWorld* world) { m_World = world; }

    // ========================================================================
    // Transform (via root component)
    // ========================================================================

    // Get/set the actor's transform (delegates to root component)
    const MiTransform& getTransform() const;
    void setTransform(const MiTransform& transform);

    // Convenience accessors
    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getScale() const;

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setScale(const glm::vec3& scale);

    // Euler angles
    glm::vec3 getEulerAngles() const;
    void setEulerAngles(const glm::vec3& eulerRadians);

    // Direction vectors
    glm::vec3 getForwardVector() const;
    glm::vec3 getRightVector() const;
    glm::vec3 getUpVector() const;

    // ========================================================================
    // Component Management
    // ========================================================================

    // Add a component of type T
    template<typename T, typename... Args>
    std::shared_ptr<T> addComponent(Args&&... args);

    // Get first component of type T
    template<typename T>
    std::shared_ptr<T> getComponent() const;

    // Get all components of type T
    template<typename T>
    std::vector<std::shared_ptr<T>> getComponents() const;

    // Check if actor has component of type T
    template<typename T>
    bool hasComponent() const;

    // Remove a specific component
    void removeComponent(const std::shared_ptr<MiComponent>& component);

    // Remove all components of type T
    template<typename T>
    void removeComponents();

    // Get all components
    const std::vector<std::shared_ptr<MiComponent>>& getAllComponents() const { return m_Components; }

    // Get component count
    size_t getComponentCount() const { return m_Components.size(); }

    // ========================================================================
    // Root Component
    // ========================================================================

    // The root scene component defines the actor's transform
    std::shared_ptr<MiSceneComponent> getRootComponent() const { return m_RootComponent; }
    void setRootComponent(std::shared_ptr<MiSceneComponent> root);

    // ========================================================================
    // Lifecycle
    // ========================================================================

    // Called after actor is fully constructed and registered with world
    virtual void beginPlay();

    // Called when actor is being removed from world
    virtual void endPlay();

    // Called every frame
    virtual void tick(float deltaTime);

    // Check if actor has begun play
    bool hasBegunPlay() const { return m_HasBegunPlay; }

    // Called when actor is registered/unregistered from world
    // Good time for components to load resources
    virtual void onRegister();
    virtual void onUnregister();

    // ========================================================================
    // Flags
    // ========================================================================

    ActorFlags getFlags() const { return m_Flags; }
    void setFlags(ActorFlags flags) { m_Flags = flags; }
    void addFlags(ActorFlags flags) { m_Flags |= flags; }
    void removeFlags(ActorFlags flags) { m_Flags = static_cast<ActorFlags>(static_cast<uint32_t>(m_Flags) & ~static_cast<uint32_t>(flags)); }

    bool isHidden() const { return hasFlag(m_Flags, ActorFlags::Hidden); }
    bool isTransient() const { return hasFlag(m_Flags, ActorFlags::Transient); }
    bool isStatic() const { return hasFlag(m_Flags, ActorFlags::Static); }
    bool isEditorOnly() const { return hasFlag(m_Flags, ActorFlags::EditorOnly); }
    bool isSelected() const { return hasFlag(m_Flags, ActorFlags::Selected); }

    void setHidden(bool hidden);
    void setSelected(bool selected);

    // ========================================================================
    // Tags
    // ========================================================================

    void addTag(const std::string& tag);
    void removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;
    const std::vector<std::string>& getTags() const { return m_Tags; }

    // ========================================================================
    // Layer
    // ========================================================================

    uint32_t getLayer() const { return m_Layer; }
    void setLayer(uint32_t layer) { m_Layer = layer; }

    // ========================================================================
    // Destruction
    // ========================================================================

    // Mark actor for destruction (will be removed from world at end of frame)
    void destroy();

    // Check if actor is being destroyed
    bool isBeingDestroyed() const { return hasFlag(m_Flags, ActorFlags::Destroying); }

    // ========================================================================
    // Serialization
    // ========================================================================

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

    // Create default root component (can be overridden in derived classes)
    virtual void createDefaultComponents();

protected:
    // Called when a component is added
    virtual void onComponentAdded(const std::shared_ptr<MiComponent>& component) {}

    // Called when a component is removed
    virtual void onComponentRemoved(const std::shared_ptr<MiComponent>& component) {}

    // Called when the root component's transform changes
    virtual void onTransformChanged() {}

private:
    // Internal component registration
    void registerComponent(const std::shared_ptr<MiComponent>& component);
    void unregisterComponent(const std::shared_ptr<MiComponent>& component);

    // Update component type cache
    void rebuildComponentTypeCache();

    MiWorld* m_World = nullptr;
    std::shared_ptr<MiSceneComponent> m_RootComponent;
    std::vector<std::shared_ptr<MiComponent>> m_Components;
    std::unordered_map<std::type_index, std::vector<size_t>> m_ComponentsByType;

    ActorFlags m_Flags = ActorFlags::None;
    std::vector<std::string> m_Tags;
    uint32_t m_Layer = 0;

    bool m_HasBegunPlay = false;

    // Default transform for actors without root component
    static MiTransform s_DefaultTransform;
};

// ============================================================================
// Template Implementations
// ============================================================================

template<typename T, typename... Args>
std::shared_ptr<T> MiActor::addComponent(Args&&... args) {
    static_assert(std::is_base_of<MiComponent, T>::value, "T must derive from MiComponent");

    auto component = std::make_shared<T>(std::forward<Args>(args)...);
    registerComponent(component);

    // If this is a scene component and we don't have a root, set it as root
    if constexpr (std::is_base_of<MiSceneComponent, T>::value) {
        if (!m_RootComponent) {
            m_RootComponent = std::static_pointer_cast<MiSceneComponent>(component);
        }
    }

    return component;
}

template<typename T>
std::shared_ptr<T> MiActor::getComponent() const {
    static_assert(std::is_base_of<MiComponent, T>::value, "T must derive from MiComponent");

    // First check the type cache for exact match
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
    static_assert(std::is_base_of<MiComponent, T>::value, "T must derive from MiComponent");

    std::vector<std::shared_ptr<T>> result;
    for (const auto& comp : m_Components) {
        if (auto cast = std::dynamic_pointer_cast<T>(comp)) {
            result.push_back(cast);
        }
    }
    return result;
}

template<typename T>
bool MiActor::hasComponent() const {
    return getComponent<T>() != nullptr;
}

template<typename T>
void MiActor::removeComponents() {
    static_assert(std::is_base_of<MiComponent, T>::value, "T must derive from MiComponent");

    auto compsToRemove = getComponents<T>();
    for (const auto& comp : compsToRemove) {
        removeComponent(comp);
    }
}

} // namespace MiEngine
