#pragma once
#include "Component.h"
#include <glm/glm.hpp>
#include <cstdint>

// Forward declarations
class Scene;

// Collider shape types
enum class ColliderShape : uint8_t {
    AABB,
    Sphere
};

// World-space AABB for physics calculations
struct WorldAABB {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    // Check intersection with another AABB
    bool intersects(const WorldAABB& other) const;

    // Get center point
    glm::vec3 getCenter() const { return (min + max) * 0.5f; }

    // Get half-extents (half-size in each direction)
    glm::vec3 getHalfExtents() const { return (max - min) * 0.5f; }
};

// World-space Sphere for physics calculations
struct WorldSphere {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.5f;

    // Check intersection with another sphere
    bool intersects(const WorldSphere& other) const;

    // Check intersection with AABB
    bool intersects(const WorldAABB& aabb) const;
};

class ColliderComponent : public Component {
public:
    ColliderComponent() = default;
    ~ColliderComponent() override = default;

    ComponentType getType() const override { return ComponentType::Collider; }

    // Shape type
    ColliderShape shape = ColliderShape::AABB;

    // Local-space parameters (relative to mesh center)
    // For AABB: half-extents in each axis
    glm::vec3 localHalfExtents = glm::vec3(0.5f);
    glm::vec3 localOffset = glm::vec3(0.0f);

    // For Sphere: radius
    float localRadius = 0.5f;

    // Collision filtering (bitmask)
    uint32_t collisionLayer = 1;        // Which layer this collider is on
    uint32_t collisionMask = 0xFFFFFFFF; // Which layers this collider collides with

    // Is this a trigger? (no physics response, only callbacks)
    bool isTrigger = false;

    // Compute world-space AABB from owner's transform
    // Requires Scene pointer to look up owner by index
    WorldAABB getWorldAABB(const Scene* scene) const;

    // Compute world-space sphere from owner's transform
    WorldSphere getWorldSphere(const Scene* scene) const;

    // Initialize collider bounds from mesh's existing AABB
    void initFromMeshAABB(const Scene* scene);

    // Check if this collider can collide with another based on layers
    bool canCollideWith(const ColliderComponent& other) const;
};
