#pragma once

#include "core/MiComponent.h"
#include "core/MiTransform.h"
#include <vector>
#include <memory>

namespace MiEngine {

// Component with a transform that can have parent/child relationships
// Similar to USceneComponent in UE5
class MiSceneComponent : public MiComponent {
    MI_OBJECT_BODY(MiSceneComponent, 201)

public:
    MiSceneComponent();
    virtual ~MiSceneComponent() = default;

    // ========================================================================
    // Local Transform (relative to parent or actor if no parent)
    // ========================================================================

    const MiTransform& getLocalTransform() const { return m_LocalTransform; }
    void setLocalTransform(const MiTransform& transform);

    glm::vec3 getLocalPosition() const { return m_LocalTransform.position; }
    glm::quat getLocalRotation() const { return m_LocalTransform.rotation; }
    glm::vec3 getLocalScale() const { return m_LocalTransform.scale; }

    void setLocalPosition(const glm::vec3& position);
    void setLocalRotation(const glm::quat& rotation);
    void setLocalScale(const glm::vec3& scale);

    // Euler angles in radians
    glm::vec3 getLocalEulerAngles() const { return m_LocalTransform.getEulerAngles(); }
    void setLocalEulerAngles(const glm::vec3& eulerRadians);

    // ========================================================================
    // World Transform (computed from hierarchy)
    // ========================================================================

    MiTransform getWorldTransform() const;
    glm::mat4 getWorldMatrix() const;

    glm::vec3 getWorldPosition() const;
    glm::quat getWorldRotation() const;
    glm::vec3 getWorldScale() const;

    void setWorldPosition(const glm::vec3& position);
    void setWorldRotation(const glm::quat& rotation);
    void setWorldScale(const glm::vec3& scale);

    // Direction vectors in world space
    glm::vec3 getForwardVector() const;
    glm::vec3 getRightVector() const;
    glm::vec3 getUpVector() const;

    // ========================================================================
    // Transform Operations
    // ========================================================================

    // Move relative to current position
    void addLocalOffset(const glm::vec3& offset);
    void addWorldOffset(const glm::vec3& offset);

    // Rotate relative to current rotation
    void addLocalRotation(const glm::quat& rotation);
    void addWorldRotation(const glm::quat& rotation);

    // Look at target
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

    // ========================================================================
    // Hierarchy
    // ========================================================================

    // Parent component (nullptr if attached directly to actor root)
    MiSceneComponent* getParent() const { return m_Parent; }

    // Attach to another scene component
    void attachTo(MiSceneComponent* parent, bool keepWorldTransform = true);
    void detachFromParent(bool keepWorldTransform = true);

    // Children
    const std::vector<MiSceneComponent*>& getChildren() const { return m_Children; }
    size_t getChildCount() const { return m_Children.size(); }
    MiSceneComponent* getChild(size_t index) const;

    // Check if this component is attached to another
    bool isAttachedTo(const MiSceneComponent* component) const;

    // ========================================================================
    // Visibility
    // ========================================================================

    bool isVisible() const { return m_Visible; }
    void setVisible(bool visible);

    // Check visibility considering parent hierarchy
    bool isVisibleInHierarchy() const;

    // ========================================================================
    // Bounds (override in derived classes for accurate bounds)
    // ========================================================================

    // Get local bounds (in component space)
    virtual glm::vec3 getLocalBoundsMin() const { return glm::vec3(-0.5f); }
    virtual glm::vec3 getLocalBoundsMax() const { return glm::vec3(0.5f); }

    // Get world bounds (transformed)
    void getWorldBounds(glm::vec3& outMin, glm::vec3& outMax) const;

    // ========================================================================
    // Serialization
    // ========================================================================

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    // Called when local transform changes
    virtual void onTransformChanged();

    // Mark world transform as needing recalculation
    void markTransformDirty();

    // Internal: add/remove child (called by attachTo/detachFromParent)
    void addChild(MiSceneComponent* child);
    void removeChild(MiSceneComponent* child);

private:
    // Recalculate cached world transform
    void updateWorldTransform() const;

    // Local transform (relative to parent)
    MiTransform m_LocalTransform;

    // Cached world transform
    mutable MiTransform m_CachedWorldTransform;
    mutable bool m_WorldTransformDirty = true;

    // Hierarchy
    MiSceneComponent* m_Parent = nullptr;
    std::vector<MiSceneComponent*> m_Children;

    // Visibility
    bool m_Visible = true;
};

} // namespace MiEngine
