#include "core/MiActor.h"
#include "core/MiComponent.h"
#include "core/MiSceneComponent.h"
#include "core/MiWorld.h"
#include "core/JsonIO.h"
#include "core/MiTypeRegistry.h"
#include <algorithm>

namespace MiEngine {

// Static default transform
MiTransform MiActor::s_DefaultTransform;

MiActor::MiActor()
    : MiObject()
    , m_World(nullptr)
    , m_RootComponent(nullptr)
    , m_Flags(ActorFlags::None)
    , m_Layer(0)
    , m_HasBegunPlay(false)
{
    setName("Actor");
}

MiActor::~MiActor() {
    // Components will be cleaned up by shared_ptr
    m_Components.clear();
    m_ComponentsByType.clear();
}

// ============================================================================
// Transform
// ============================================================================

const MiTransform& MiActor::getTransform() const {
    if (m_RootComponent) {
        return m_RootComponent->getLocalTransform();
    }
    return s_DefaultTransform;
}

void MiActor::setTransform(const MiTransform& transform) {
    if (m_RootComponent) {
        m_RootComponent->setLocalTransform(transform);
        onTransformChanged();
    }
}

glm::vec3 MiActor::getPosition() const {
    if (m_RootComponent) {
        return m_RootComponent->getWorldPosition();
    }
    return glm::vec3(0.0f);
}

glm::quat MiActor::getRotation() const {
    if (m_RootComponent) {
        return m_RootComponent->getWorldRotation();
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 MiActor::getScale() const {
    if (m_RootComponent) {
        return m_RootComponent->getWorldScale();
    }
    return glm::vec3(1.0f);
}

void MiActor::setPosition(const glm::vec3& position) {
    if (m_RootComponent) {
        m_RootComponent->setWorldPosition(position);
        onTransformChanged();
    }
}

void MiActor::setRotation(const glm::quat& rotation) {
    if (m_RootComponent) {
        m_RootComponent->setWorldRotation(rotation);
        onTransformChanged();
    }
}

void MiActor::setScale(const glm::vec3& scale) {
    if (m_RootComponent) {
        m_RootComponent->setWorldScale(scale);
        onTransformChanged();
    }
}

glm::vec3 MiActor::getEulerAngles() const {
    if (m_RootComponent) {
        return m_RootComponent->getLocalEulerAngles();
    }
    return glm::vec3(0.0f);
}

void MiActor::setEulerAngles(const glm::vec3& eulerRadians) {
    if (m_RootComponent) {
        m_RootComponent->setLocalEulerAngles(eulerRadians);
        onTransformChanged();
    }
}

glm::vec3 MiActor::getForwardVector() const {
    if (m_RootComponent) {
        return m_RootComponent->getForwardVector();
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 MiActor::getRightVector() const {
    if (m_RootComponent) {
        return m_RootComponent->getRightVector();
    }
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 MiActor::getUpVector() const {
    if (m_RootComponent) {
        return m_RootComponent->getUpVector();
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

// ============================================================================
// Component Management
// ============================================================================

void MiActor::registerComponent(const std::shared_ptr<MiComponent>& component) {
    if (!component) {
        return;
    }

    // Set owner
    component->setOwner(this);

    // Add to component list
    m_Components.push_back(component);

    // Update type cache
    m_ComponentsByType[typeid(*component)].push_back(m_Components.size() - 1);

    // Call lifecycle
    component->onAttached();

    // If we've already begun play, call beginPlay on the component
    if (m_HasBegunPlay) {
        component->beginPlay();
    }

    // Notify derived classes
    onComponentAdded(component);

    markDirty();
}

void MiActor::unregisterComponent(const std::shared_ptr<MiComponent>& component) {
    if (!component) {
        return;
    }

    // Call lifecycle
    if (m_HasBegunPlay) {
        component->endPlay();
    }
    component->onDetached();

    // Remove from owner
    component->setOwner(nullptr);

    // Notify derived classes
    onComponentRemoved(component);
}

void MiActor::removeComponent(const std::shared_ptr<MiComponent>& component) {
    if (!component) {
        return;
    }

    auto it = std::find(m_Components.begin(), m_Components.end(), component);
    if (it != m_Components.end()) {
        unregisterComponent(component);

        // If this is the root component, clear it
        if (m_RootComponent == component) {
            m_RootComponent = nullptr;
        }

        m_Components.erase(it);
        rebuildComponentTypeCache();
        markDirty();
    }
}

void MiActor::rebuildComponentTypeCache() {
    m_ComponentsByType.clear();
    for (size_t i = 0; i < m_Components.size(); ++i) {
        m_ComponentsByType[typeid(*m_Components[i])].push_back(i);
    }
}

void MiActor::setRootComponent(std::shared_ptr<MiSceneComponent> root) {
    if (root == m_RootComponent) {
        return;
    }

    m_RootComponent = root;

    // Make sure the root is in our component list
    if (root) {
        auto it = std::find(m_Components.begin(), m_Components.end(), root);
        if (it == m_Components.end()) {
            registerComponent(root);
        }
    }

    markDirty();
}

// ============================================================================
// Lifecycle
// ============================================================================

void MiActor::createDefaultComponents() {
    // Create a default root scene component if none exists
    if (!m_RootComponent) {
        auto root = std::make_shared<MiSceneComponent>();
        root->setName("DefaultRoot");
        setRootComponent(root);
    }
}

void MiActor::onRegister() {
    // Called when actor is registered to world
    // Notify all components - good time to load resources
    for (auto& component : m_Components) {
        component->onRegister();
    }
}

void MiActor::onUnregister() {
    // Called when actor is unregistered from world
    for (auto& component : m_Components) {
        component->onUnregister();
    }
}

void MiActor::beginPlay() {
    if (m_HasBegunPlay) {
        return;
    }

    m_HasBegunPlay = true;

    // Call beginPlay on all components
    for (auto& component : m_Components) {
        component->beginPlay();
    }
}

void MiActor::endPlay() {
    if (!m_HasBegunPlay) {
        return;
    }

    // Call endPlay on all components
    for (auto& component : m_Components) {
        component->endPlay();
    }

    m_HasBegunPlay = false;
}

void MiActor::tick(float deltaTime) {
    // Tick all tickable components
    for (auto& component : m_Components) {
        if (component->isEnabled() && component->isTickable()) {
            component->tick(deltaTime);
        }
    }
}

// ============================================================================
// Flags
// ============================================================================

void MiActor::setHidden(bool hidden) {
    if (hidden) {
        addFlags(ActorFlags::Hidden);
    } else {
        removeFlags(ActorFlags::Hidden);
    }
}

void MiActor::setSelected(bool selected) {
    if (selected) {
        addFlags(ActorFlags::Selected);
    } else {
        removeFlags(ActorFlags::Selected);
    }
}

// ============================================================================
// Tags
// ============================================================================

void MiActor::addTag(const std::string& tag) {
    if (!hasTag(tag)) {
        m_Tags.push_back(tag);
        markDirty();
    }
}

void MiActor::removeTag(const std::string& tag) {
    auto it = std::find(m_Tags.begin(), m_Tags.end(), tag);
    if (it != m_Tags.end()) {
        m_Tags.erase(it);
        markDirty();
    }
}

bool MiActor::hasTag(const std::string& tag) const {
    return std::find(m_Tags.begin(), m_Tags.end(), tag) != m_Tags.end();
}

// ============================================================================
// Destruction
// ============================================================================

void MiActor::destroy() {
    if (isBeingDestroyed()) {
        return;
    }

    addFlags(ActorFlags::Destroying);
    markPendingDestroy();

    // The world will handle actual removal at the end of the frame
    if (m_World) {
        // World::destroyActor will be called by the world's update loop
    }
}

// ============================================================================
// Serialization
// ============================================================================

void MiActor::serialize(JsonWriter& writer) const {
    // Base object data
    MiObject::serialize(writer);

    // Actor-specific data
    writer.writeUInt("flags", static_cast<uint32_t>(m_Flags) & ~static_cast<uint32_t>(ActorFlags::Selected | ActorFlags::Spawning | ActorFlags::Destroying));
    writer.writeUInt("layer", m_Layer);

    // Tags
    writer.beginArray("tags");
    for (const auto& tag : m_Tags) {
        writer.writeArrayString(tag);
    }
    writer.endArray();

    // Transform (from root component)
    if (m_RootComponent) {
        writer.beginObject("transform");
        m_RootComponent->getLocalTransform().serialize(writer);
        writer.endObject();
    }

    // Components
    writer.beginArray("components");
    for (const auto& component : m_Components) {
        writer.beginArrayObject();
        component->serialize(writer);
        writer.endObject();
    }
    writer.endArray();
}

void MiActor::deserialize(const JsonReader& reader) {
    // Base object data
    MiObject::deserialize(reader);

    // Actor-specific data
    m_Flags = static_cast<ActorFlags>(reader.getUInt("flags", 0));
    m_Layer = reader.getUInt("layer", 0);

    // Tags
    m_Tags = reader.getStringArray("tags");

    // Transform
    JsonReader transformReader = reader.getObject("transform");
    MiTransform transform;
    if (transformReader.isValid()) {
        transform.deserialize(transformReader);
    }

    // Create default components if they don't exist yet
    if (m_Components.empty()) {
        createDefaultComponents();
    }

    // Apply transform to root
    if (m_RootComponent) {
        m_RootComponent->setLocalTransform(transform);
    }

    // Deserialize components
    auto componentReaders = reader.getArray("components");
    for (const auto& compReader : componentReaders) {
        std::string compTypeName = compReader.getString("type", "");
        if (compTypeName.empty()) continue;

        // Find existing component of this type and deserialize into it
        for (auto& component : m_Components) {
            if (component->getTypeName() == compTypeName) {
                component->deserialize(compReader);
                break;
            }
        }
    }
}

// Register the type
MI_REGISTER_TYPE(MiActor)

} // namespace MiEngine
