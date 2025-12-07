#pragma once
#include <vector>
#include <functional>
#include <glm/glm.hpp>

// Forward declarations
class Scene;
class RigidBodyComponent;
class ColliderComponent;
struct MeshInstance;
struct WorldAABB;

// Information about a collision between two objects
struct CollisionInfo {
    MeshInstance* instanceA = nullptr;
    MeshInstance* instanceB = nullptr;
    ColliderComponent* colliderA = nullptr;
    ColliderComponent* colliderB = nullptr;
    glm::vec3 contactPoint = glm::vec3(0.0f);
    glm::vec3 contactNormal = glm::vec3(0.0f); // Points from A to B
    float penetrationDepth = 0.0f;
};

// Callback type for collision events
using CollisionCallback = std::function<void(const CollisionInfo&)>;

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld() = default;

    // World configuration
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float fixedTimeStep = 1.0f / 60.0f; // 60 Hz physics update

    // Link to scene for accessing MeshInstances
    void setScene(Scene* scene) { m_Scene = scene; }
    Scene* getScene() const { return m_Scene; }

    // Main physics update (uses fixed timestep with accumulator)
    void update(float deltaTime);

    // Single physics step (call directly for fixed-step games)
    void step(float dt);

    // Collision callbacks
    void setCollisionCallback(CollisionCallback callback) { m_CollisionCallback = callback; }
    void setTriggerCallback(CollisionCallback callback) { m_TriggerCallback = callback; }

    // Query methods for game logic
    std::vector<MeshInstance*> queryAABB(const WorldAABB& aabb) const;
    std::vector<MeshInstance*> querySphere(const glm::vec3& center, float radius) const;

    // Raycast (returns true if hit)
    bool raycast(const glm::vec3& origin,
                 const glm::vec3& direction,
                 float maxDistance,
                 MeshInstance*& hitInstance,
                 glm::vec3& hitPoint,
                 glm::vec3& hitNormal) const;

    // Debug / statistics
    int getActiveBodyCount() const { return m_ActiveBodyCount; }
    int getCollisionCount() const { return m_CollisionCount; }

private:
    Scene* m_Scene = nullptr;
    float m_TimeAccumulator = 0.0f;

    CollisionCallback m_CollisionCallback;
    CollisionCallback m_TriggerCallback;

    // Statistics
    int m_ActiveBodyCount = 0;
    int m_CollisionCount = 0;

    // Internal physics steps
    void integrateForces(float dt);
    void integrateVelocities(float dt);
    void detectCollisions(std::vector<CollisionInfo>& collisions);
    void resolveCollisions(const std::vector<CollisionInfo>& collisions);
    void syncTransforms();
};
