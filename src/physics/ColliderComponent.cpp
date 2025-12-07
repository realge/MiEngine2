#include "physics/ColliderComponent.h"
#include "scene/Scene.h"
#include "mesh/Mesh.h"

// WorldAABB methods
bool WorldAABB::intersects(const WorldAABB& other) const {
    // AABB intersection test
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
}

// WorldSphere methods
bool WorldSphere::intersects(const WorldSphere& other) const {
    // Sphere-sphere intersection: distance between centers < sum of radii
    glm::vec3 diff = center - other.center;
    float distSq = glm::dot(diff, diff);
    float radiusSum = radius + other.radius;
    return distSq <= (radiusSum * radiusSum);
}

bool WorldSphere::intersects(const WorldAABB& aabb) const {
    // Find closest point on AABB to sphere center
    glm::vec3 closest;
    closest.x = glm::clamp(center.x, aabb.min.x, aabb.max.x);
    closest.y = glm::clamp(center.y, aabb.min.y, aabb.max.y);
    closest.z = glm::clamp(center.z, aabb.min.z, aabb.max.z);

    // Check if closest point is within sphere radius
    glm::vec3 diff = center - closest;
    float distSq = glm::dot(diff, diff);
    return distSq <= (radius * radius);
}

// ColliderComponent methods
WorldAABB ColliderComponent::getWorldAABB(const Scene* scene) const {
    WorldAABB result;

    if (!scene || !hasOwner()) {
        // No owner, return local bounds centered at origin
        result.min = localOffset - localHalfExtents;
        result.max = localOffset + localHalfExtents;
        return result;
    }

    // Get owner's transform via index lookup
    const auto& instances = scene->getMeshInstances();
    if (ownerIndex >= instances.size()) {
        result.min = localOffset - localHalfExtents;
        result.max = localOffset + localHalfExtents;
        return result;
    }

    const Transform& t = instances[ownerIndex].transform;

    // Apply scale to half-extents
    glm::vec3 scaledHalfExtents = localHalfExtents * t.scale;

    // Compute world-space center (position + scaled offset)
    glm::vec3 worldCenter = t.position + (localOffset * t.scale);

    // For now, ignore rotation (axis-aligned assumption)
    // TODO: For rotated AABBs, compute OBB or expanded AABB
    result.min = worldCenter - scaledHalfExtents;
    result.max = worldCenter + scaledHalfExtents;

    return result;
}

WorldSphere ColliderComponent::getWorldSphere(const Scene* scene) const {
    WorldSphere result;

    if (!scene || !hasOwner()) {
        result.center = localOffset;
        result.radius = localRadius;
        return result;
    }

    const auto& instances = scene->getMeshInstances();
    if (ownerIndex >= instances.size()) {
        result.center = localOffset;
        result.radius = localRadius;
        return result;
    }

    const Transform& t = instances[ownerIndex].transform;

    // World center
    result.center = t.position + (localOffset * t.scale);

    // Use maximum scale component for radius (conservative)
    float maxScale = glm::max(t.scale.x, glm::max(t.scale.y, t.scale.z));
    result.radius = localRadius * maxScale;

    return result;
}

void ColliderComponent::initFromMeshAABB(const Scene* scene) {
    if (!scene || !hasOwner()) {
        return;
    }

    const auto& instances = scene->getMeshInstances();
    if (ownerIndex >= instances.size()) {
        return;
    }

    const auto& mesh = instances[ownerIndex].mesh;
    if (!mesh) {
        return;
    }

    // Get the mesh's local-space bounding box
    const AABB& meshAABB = mesh->getBoundingBox();

    // Compute half-extents from mesh AABB
    localHalfExtents = meshAABB.getExtents();

    // Compute offset (if mesh AABB is not centered at origin)
    localOffset = meshAABB.getCenter();

    // For sphere colliders, use the maximum extent as radius
    localRadius = glm::max(localHalfExtents.x,
                          glm::max(localHalfExtents.y, localHalfExtents.z));
}

bool ColliderComponent::canCollideWith(const ColliderComponent& other) const {
    // Check if this collider's layer is in the other's mask, and vice versa
    return (collisionLayer & other.collisionMask) != 0 &&
           (other.collisionLayer & collisionMask) != 0;
}
