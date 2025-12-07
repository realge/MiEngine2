#include "core/MiSceneComponent.h"
#include "core/MiActor.h"
#include "core/JsonIO.h"
#include "core/MiTypeRegistry.h"
#include <algorithm>

namespace MiEngine {

MiSceneComponent::MiSceneComponent()
    : MiComponent()
    , m_LocalTransform()
    , m_CachedWorldTransform()
    , m_WorldTransformDirty(true)
    , m_Parent(nullptr)
    , m_Visible(true)
{
    setName("SceneComponent");
}

// ============================================================================
// Local Transform
// ============================================================================

void MiSceneComponent::setLocalTransform(const MiTransform& transform) {
    m_LocalTransform = transform;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setLocalPosition(const glm::vec3& position) {
    m_LocalTransform.position = position;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setLocalRotation(const glm::quat& rotation) {
    m_LocalTransform.rotation = rotation;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setLocalScale(const glm::vec3& scale) {
    m_LocalTransform.scale = scale;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setLocalEulerAngles(const glm::vec3& eulerRadians) {
    m_LocalTransform.setEulerAngles(eulerRadians);
    markTransformDirty();
    onTransformChanged();
}

// ============================================================================
// World Transform
// ============================================================================

void MiSceneComponent::updateWorldTransform() const {
    if (!m_WorldTransformDirty) {
        return;
    }

    if (m_Parent) {
        // Combine with parent's world transform
        MiTransform parentWorld = m_Parent->getWorldTransform();
        m_CachedWorldTransform = parentWorld * m_LocalTransform;
    } else {
        // No parent, local is world
        m_CachedWorldTransform = m_LocalTransform;
    }

    m_WorldTransformDirty = false;
}

MiTransform MiSceneComponent::getWorldTransform() const {
    updateWorldTransform();
    return m_CachedWorldTransform;
}

glm::mat4 MiSceneComponent::getWorldMatrix() const {
    return getWorldTransform().getMatrix();
}

glm::vec3 MiSceneComponent::getWorldPosition() const {
    return getWorldTransform().position;
}

glm::quat MiSceneComponent::getWorldRotation() const {
    return getWorldTransform().rotation;
}

glm::vec3 MiSceneComponent::getWorldScale() const {
    return getWorldTransform().scale;
}

void MiSceneComponent::setWorldPosition(const glm::vec3& position) {
    if (m_Parent) {
        // Convert world position to local
        MiTransform parentWorld = m_Parent->getWorldTransform();
        m_LocalTransform.position = parentWorld.inverseTransformPoint(position);
    } else {
        m_LocalTransform.position = position;
    }
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setWorldRotation(const glm::quat& rotation) {
    if (m_Parent) {
        // Convert world rotation to local
        glm::quat parentWorldRot = m_Parent->getWorldRotation();
        m_LocalTransform.rotation = glm::inverse(parentWorldRot) * rotation;
    } else {
        m_LocalTransform.rotation = rotation;
    }
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::setWorldScale(const glm::vec3& scale) {
    if (m_Parent) {
        // Convert world scale to local
        glm::vec3 parentWorldScale = m_Parent->getWorldScale();
        m_LocalTransform.scale = scale / parentWorldScale;
    } else {
        m_LocalTransform.scale = scale;
    }
    markTransformDirty();
    onTransformChanged();
}

glm::vec3 MiSceneComponent::getForwardVector() const {
    return getWorldTransform().getForward();
}

glm::vec3 MiSceneComponent::getRightVector() const {
    return getWorldTransform().getRight();
}

glm::vec3 MiSceneComponent::getUpVector() const {
    return getWorldTransform().getUp();
}

// ============================================================================
// Transform Operations
// ============================================================================

void MiSceneComponent::addLocalOffset(const glm::vec3& offset) {
    m_LocalTransform.position += offset;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::addWorldOffset(const glm::vec3& offset) {
    setWorldPosition(getWorldPosition() + offset);
}

void MiSceneComponent::addLocalRotation(const glm::quat& rotation) {
    m_LocalTransform.rotation = m_LocalTransform.rotation * rotation;
    markTransformDirty();
    onTransformChanged();
}

void MiSceneComponent::addWorldRotation(const glm::quat& rotation) {
    setWorldRotation(rotation * getWorldRotation());
}

void MiSceneComponent::lookAt(const glm::vec3& target, const glm::vec3& up) {
    glm::vec3 worldPos = getWorldPosition();
    glm::vec3 direction = glm::normalize(target - worldPos);

    if (glm::length(direction) < 0.0001f) {
        return;  // Target is at our position
    }

    // Create rotation from direction
    glm::vec3 right = glm::normalize(glm::cross(up, direction));
    glm::vec3 correctedUp = glm::cross(direction, right);

    glm::mat3 rotMatrix(right, correctedUp, direction);
    glm::quat worldRot = glm::quat_cast(rotMatrix);

    setWorldRotation(worldRot);
}

// ============================================================================
// Hierarchy
// ============================================================================

void MiSceneComponent::attachTo(MiSceneComponent* parent, bool keepWorldTransform) {
    if (parent == this || parent == m_Parent) {
        return;  // Can't attach to self or already attached
    }

    // Check for circular attachment
    if (parent && parent->isAttachedTo(this)) {
        return;  // Would create a cycle
    }

    MiTransform worldTransform;
    if (keepWorldTransform) {
        worldTransform = getWorldTransform();
    }

    // Detach from current parent
    if (m_Parent) {
        m_Parent->removeChild(this);
    }

    // Attach to new parent
    m_Parent = parent;
    if (m_Parent) {
        m_Parent->addChild(this);
    }

    // Restore world transform if requested
    if (keepWorldTransform && m_Parent) {
        MiTransform parentWorld = m_Parent->getWorldTransform();
        m_LocalTransform = parentWorld.inverse() * worldTransform;
    }

    markTransformDirty();
}

void MiSceneComponent::detachFromParent(bool keepWorldTransform) {
    if (!m_Parent) {
        return;
    }

    MiTransform worldTransform;
    if (keepWorldTransform) {
        worldTransform = getWorldTransform();
    }

    m_Parent->removeChild(this);
    m_Parent = nullptr;

    if (keepWorldTransform) {
        m_LocalTransform = worldTransform;
    }

    markTransformDirty();
}

MiSceneComponent* MiSceneComponent::getChild(size_t index) const {
    if (index < m_Children.size()) {
        return m_Children[index];
    }
    return nullptr;
}

bool MiSceneComponent::isAttachedTo(const MiSceneComponent* component) const {
    const MiSceneComponent* current = m_Parent;
    while (current) {
        if (current == component) {
            return true;
        }
        current = current->m_Parent;
    }
    return false;
}

void MiSceneComponent::addChild(MiSceneComponent* child) {
    if (child && std::find(m_Children.begin(), m_Children.end(), child) == m_Children.end()) {
        m_Children.push_back(child);
    }
}

void MiSceneComponent::removeChild(MiSceneComponent* child) {
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end()) {
        m_Children.erase(it);
    }
}

// ============================================================================
// Visibility
// ============================================================================

void MiSceneComponent::setVisible(bool visible) {
    if (m_Visible != visible) {
        m_Visible = visible;
        markDirty();
    }
}

bool MiSceneComponent::isVisibleInHierarchy() const {
    if (!m_Visible) {
        return false;
    }
    if (m_Parent) {
        return m_Parent->isVisibleInHierarchy();
    }
    return true;
}

// ============================================================================
// Bounds
// ============================================================================

void MiSceneComponent::getWorldBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    glm::vec3 localMin = getLocalBoundsMin();
    glm::vec3 localMax = getLocalBoundsMax();

    glm::mat4 worldMatrix = getWorldMatrix();

    // Transform all 8 corners and find min/max
    glm::vec3 corners[8] = {
        glm::vec3(localMin.x, localMin.y, localMin.z),
        glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z),
        glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z),
        glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z),
        glm::vec3(localMax.x, localMax.y, localMax.z)
    };

    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);

    for (const auto& corner : corners) {
        glm::vec3 worldCorner = glm::vec3(worldMatrix * glm::vec4(corner, 1.0f));
        outMin = glm::min(outMin, worldCorner);
        outMax = glm::max(outMax, worldCorner);
    }
}

// ============================================================================
// Transform Dirty
// ============================================================================

void MiSceneComponent::markTransformDirty() {
    m_WorldTransformDirty = true;
    markDirty();

    // Mark all children as dirty too
    for (auto* child : m_Children) {
        child->markTransformDirty();
    }
}

void MiSceneComponent::onTransformChanged() {
    // Notify owner if we have one
    // This allows actors to respond to component transform changes
}

// ============================================================================
// Serialization
// ============================================================================

void MiSceneComponent::serialize(JsonWriter& writer) const {
    MiComponent::serialize(writer);

    writer.beginObject("transform");
    m_LocalTransform.serialize(writer);
    writer.endObject();

    writer.writeBool("visible", m_Visible);

    // Note: Parent reference is not serialized here - it's handled at the actor level
}

void MiSceneComponent::deserialize(const JsonReader& reader) {
    MiComponent::deserialize(reader);

    JsonReader transformReader = reader.getObject("transform");
    if (transformReader.isValid()) {
        m_LocalTransform.deserialize(transformReader);
    }

    m_Visible = reader.getBool("visible", true);

    markTransformDirty();
}

// Register the type
MI_REGISTER_TYPE(MiSceneComponent)

} // namespace MiEngine
