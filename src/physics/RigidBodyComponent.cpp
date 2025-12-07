#include "physics/RigidBodyComponent.h"

void RigidBodyComponent::addForce(const glm::vec3& force) {
    if (bodyType != RigidBodyType::Dynamic) {
        return; // Only dynamic bodies respond to forces
    }
    forceAccumulator += force;
}

void RigidBodyComponent::addImpulse(const glm::vec3& impulse) {
    if (bodyType != RigidBodyType::Dynamic) {
        return;
    }
    // Impulse directly changes velocity: v += impulse / mass
    velocity += impulse * inverseMass;
}

void RigidBodyComponent::setMass(float newMass) {
    mass = newMass;

    // Update inverse mass
    if (mass > 0.0f && bodyType == RigidBodyType::Dynamic) {
        inverseMass = 1.0f / mass;
    } else {
        inverseMass = 0.0f; // Infinite mass for static/kinematic
    }
}

void RigidBodyComponent::clearForces() {
    forceAccumulator = glm::vec3(0.0f);
}

glm::vec3 RigidBodyComponent::applyConstraints(const glm::vec3& delta) const {
    glm::vec3 result = delta;

    if (lockPositionX) result.x = 0.0f;
    if (lockPositionY) result.y = 0.0f;
    if (lockPositionZ) result.z = 0.0f;

    return result;
}
