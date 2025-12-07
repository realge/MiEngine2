#include "physics/PhysicsWorld.h"
#include "physics/RigidBodyComponent.h"
#include "physics/ColliderComponent.h"
#include "scene/Scene.h"
#include <iostream>
#include <cmath>

PhysicsWorld::PhysicsWorld() {
    // Default gravity (Earth-like)
    gravity = glm::vec3(0.0f, -9.81f, 0.0f);
}

void PhysicsWorld::update(float deltaTime) {
    if (!m_Scene) {
        return;
    }

    // Fixed timestep with accumulator for deterministic physics
    m_TimeAccumulator += deltaTime;

    while (m_TimeAccumulator >= fixedTimeStep) {
        step(fixedTimeStep);
        m_TimeAccumulator -= fixedTimeStep;
    }
}

void PhysicsWorld::step(float dt) {
    if (!m_Scene) {
        return;
    }

    m_ActiveBodyCount = 0;
    m_CollisionCount = 0;

    // Step 1: Apply forces (gravity, etc.) to all dynamic bodies
    integrateForces(dt);

    // Step 2: Detect collisions
    std::vector<CollisionInfo> collisions;
    detectCollisions(collisions);
    m_CollisionCount = static_cast<int>(collisions.size());

    // Step 3: Resolve collisions
    resolveCollisions(collisions);

    // Step 4: Integrate velocities to update positions
    integrateVelocities(dt);

    // Step 5: Sync physics state back to MeshInstance transforms
    syncTransforms();
}

void PhysicsWorld::integrateForces(float dt) {
    const auto& instances = m_Scene->getMeshInstances();

    for (size_t i = 0; i < instances.size(); i++) {
        const auto& instance = instances[i];
        if (!instance.rigidBody || !instance.rigidBody->enabled) {
            continue;
        }

        auto* rb = instance.rigidBody.get();
        if (rb->bodyType != RigidBodyType::Dynamic) {
            continue;
        }

        m_ActiveBodyCount++;

        // Apply gravity
        rb->forceAccumulator += gravity * rb->mass * rb->gravityScale;

        // Integrate forces to velocity
        rb->acceleration = rb->forceAccumulator * rb->inverseMass;
        rb->velocity += rb->acceleration * dt;

        // Apply damping (frame-rate independent)
        // Using exponential decay: v *= e^(-damping * dt)
        float dampingFactor = expf(-rb->linearDamping * dt);
        rb->velocity *= dampingFactor;

        // Clear forces for next frame
        rb->clearForces();
    }
}

void PhysicsWorld::detectCollisions(std::vector<CollisionInfo>& collisions) {
    // TODO: Implement collision detection
    // For now, this is a skeleton - no actual collision detection

    // Future implementation:
    // - Broad phase: Check AABB overlaps for all pairs
    // - Narrow phase: Detailed collision test based on shape types
    // - Generate CollisionInfo for each collision
}

void PhysicsWorld::resolveCollisions(const std::vector<CollisionInfo>& collisions) {
    // TODO: Implement collision resolution
    // For now, just fire callbacks

    for (const auto& collision : collisions) {
        bool isTrigger = (collision.colliderA && collision.colliderA->isTrigger) ||
                        (collision.colliderB && collision.colliderB->isTrigger);

        if (isTrigger) {
            if (m_TriggerCallback) {
                m_TriggerCallback(collision);
            }
        } else {
            if (m_CollisionCallback) {
                m_CollisionCallback(collision);
            }
            // TODO: Apply impulse-based collision response
        }
    }
}

void PhysicsWorld::integrateVelocities(float dt) {
    // Need non-const reference to modify positions
    // This is safe because we're the physics system owned by Scene
    auto& instances = const_cast<std::vector<MeshInstance>&>(m_Scene->getMeshInstances());

    for (size_t i = 0; i < instances.size(); i++) {
        auto& instance = instances[i];
        if (!instance.rigidBody || !instance.rigidBody->enabled) {
            continue;
        }

        auto* rb = instance.rigidBody.get();
        if (rb->bodyType != RigidBodyType::Dynamic) {
            continue;
        }

        // Compute position delta
        glm::vec3 delta = rb->velocity * dt;

        // Apply constraints
        delta = rb->applyConstraints(delta);

        // Update transform position
        instance.transform.position += delta;
    }
}

void PhysicsWorld::syncTransforms() {
    // Transform sync is done in integrateVelocities for now
    // This method can be used for additional sync logic if needed
}

std::vector<MeshInstance*> PhysicsWorld::queryAABB(const WorldAABB& aabb) const {
    std::vector<MeshInstance*> results;

    // TODO: Implement AABB query
    // For each MeshInstance with a collider:
    //   - Get world AABB
    //   - If intersects with query AABB, add to results

    return results;
}

std::vector<MeshInstance*> PhysicsWorld::querySphere(const glm::vec3& center, float radius) const {
    std::vector<MeshInstance*> results;

    // TODO: Implement sphere query
    // For each MeshInstance with a collider:
    //   - Get world bounds
    //   - If intersects with query sphere, add to results

    return results;
}

bool PhysicsWorld::raycast(const glm::vec3& origin,
                           const glm::vec3& direction,
                           float maxDistance,
                           MeshInstance*& hitInstance,
                           glm::vec3& hitPoint,
                           glm::vec3& hitNormal) const {
    // TODO: Implement raycast
    // For each MeshInstance with a collider:
    //   - Test ray against collider shape
    //   - Track closest hit

    hitInstance = nullptr;
    return false;
}
