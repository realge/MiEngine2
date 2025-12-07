#pragma once
#include "Component.h"
#include <glm/glm.hpp>

// Body type determines how physics simulation affects the object
enum class RigidBodyType : uint8_t {
    Dynamic,   // Fully simulated: responds to gravity, forces, and collisions
    Kinematic, // Moved by code only, affects dynamic bodies but ignores forces
    Static     // Never moves, used for terrain, walls, etc.
};

class RigidBodyComponent : public Component {
public:
    RigidBodyComponent() = default;
    ~RigidBodyComponent() override = default;

    ComponentType getType() const override { return ComponentType::RigidBody; }

    // Body type
    RigidBodyType bodyType = RigidBodyType::Dynamic;

    // Physical properties
    float mass = 1.0f;
    float inverseMass = 1.0f;      // Cached: 1/mass, 0 for static/kinematic
    float restitution = 0.3f;      // Bounciness: 0 = no bounce, 1 = perfect bounce
    float friction = 0.5f;         // Surface friction coefficient
    float linearDamping = 0.01f;   // Air resistance / drag

    // Dynamics state
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 acceleration = glm::vec3(0.0f);
    glm::vec3 forceAccumulator = glm::vec3(0.0f);

    // Gravity scale: 0 = no gravity, 1 = normal, -1 = reverse gravity
    float gravityScale = 1.0f;

    // Position constraints (lock movement on specific axes)
    bool lockPositionX = false;
    bool lockPositionY = false;
    bool lockPositionZ = false;

    // Apply a continuous force (will be integrated over time)
    void addForce(const glm::vec3& force);

    // Apply an instant impulse (immediate velocity change)
    void addImpulse(const glm::vec3& impulse);

    // Set mass and update inverse mass accordingly
    void setMass(float newMass);

    // Clear accumulated forces (called after physics step)
    void clearForces();

    // Apply position constraints to a delta movement
    glm::vec3 applyConstraints(const glm::vec3& delta) const;

    // Check if this body should be simulated
    bool isSimulated() const { return bodyType == RigidBodyType::Dynamic && enabled; }
};
