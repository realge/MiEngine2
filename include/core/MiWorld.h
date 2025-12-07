#pragma once

#include "core/MiObject.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>

// Forward declare VulkanRenderer (global namespace)
class VulkanRenderer;

namespace MiEngine {

// Forward declarations
class MiActor;
class PhysicsWorld;
class JsonWriter;
class JsonReader;

// Light structure for world lighting
struct MiLight {
    glm::vec3 position = glm::vec3(0.0f);   // Position (point) or direction (directional)
    glm::vec3 color = glm::vec3(1.0f);      // Light color
    float intensity = 1.0f;                  // Light intensity
    float radius = 10.0f;                    // Falloff radius (point lights)
    float falloff = 1.0f;                    // Falloff exponent
    bool isDirectional = false;              // true = directional, false = point light
};

// World settings
struct WorldSettings {
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float physicsTimeStep = 1.0f / 60.0f;
    bool enablePhysics = true;
    glm::vec3 ambientColor = glm::vec3(0.1f, 0.1f, 0.1f);
    std::string skyboxPath;
    std::string environmentHDR;  // HDR environment map for IBL

    void serialize(JsonWriter& writer) const;
    void deserialize(const JsonReader& reader);
};

// Main world class containing all actors (similar to UWorld in UE5)
class MiWorld : public MiObject {
    MI_OBJECT_BODY(MiWorld, 50)

public:
    MiWorld();
    ~MiWorld();

    // ========================================================================
    // Initialization
    // ========================================================================

    void initialize(VulkanRenderer* renderer = nullptr);
    void shutdown();

    bool isInitialized() const { return m_Initialized; }

    // ========================================================================
    // Actor Management
    // ========================================================================

    // Spawn an actor of type T
    template<typename T, typename... Args>
    std::shared_ptr<T> spawnActor(Args&&... args);

    // Spawn an actor by type name (for serialization)
    std::shared_ptr<MiActor> spawnActorByTypeName(const std::string& typeName);

    // Destroy an actor (deferred until end of frame)
    void destroyActor(std::shared_ptr<MiActor> actor);
    void destroyActor(const ObjectId& id);

    // Destroy all actors
    void destroyAllActors();

    // ========================================================================
    // Actor Queries
    // ========================================================================

    // Find actor by ID
    std::shared_ptr<MiActor> findActorById(const ObjectId& id) const;

    // Find actor by name (returns first match)
    std::shared_ptr<MiActor> findActorByName(const std::string& name) const;

    // Find all actors with a specific tag
    std::vector<std::shared_ptr<MiActor>> findActorsByTag(const std::string& tag) const;

    // Find all actors on a specific layer
    std::vector<std::shared_ptr<MiActor>> findActorsByLayer(uint32_t layer) const;

    // Find all actors of type T
    template<typename T>
    std::vector<std::shared_ptr<T>> findActorsOfType() const;

    // Get all actors
    const std::vector<std::shared_ptr<MiActor>>& getAllActors() const { return m_Actors; }

    // Get actor count
    size_t getActorCount() const { return m_Actors.size(); }

    // ========================================================================
    // Iteration
    // ========================================================================

    void forEachActor(const std::function<void(MiActor*)>& callback);
    void forEachActor(const std::function<void(const MiActor*)>& callback) const;

    // ========================================================================
    // Update Loop
    // ========================================================================

    // Start simulation
    void beginPlay();

    // Stop simulation
    void endPlay();

    // Update world (call every frame)
    void tick(float deltaTime);

    // Check if world is playing
    bool isPlaying() const { return m_IsPlaying; }

    // ========================================================================
    // Physics (placeholder for now)
    // ========================================================================

    // PhysicsWorld* getPhysicsWorld() const { return m_PhysicsWorld.get(); }

    // ========================================================================
    // Settings
    // ========================================================================

    const WorldSettings& getSettings() const { return m_Settings; }
    void setSettings(const WorldSettings& settings) { m_Settings = settings; markDirty(); }

    // ========================================================================
    // Lighting
    // ========================================================================

    // Add a light to the world
    void addLight(const glm::vec3& positionOrDirection, const glm::vec3& color,
                  float intensity = 1.0f, float radius = 10.0f,
                  float falloff = 1.0f, bool isDirectional = false);

    // Add a light using MiLight struct
    void addLight(const MiLight& light);

    // Remove a light by index
    void removeLight(size_t index);

    // Clear all lights
    void clearLights();

    // Setup default lighting (directional sun + ambient)
    void setupDefaultLighting();

    // Get lights
    const std::vector<MiLight>& getLights() const { return m_Lights; }
    std::vector<MiLight>& getLights() { return m_Lights; }

    // ========================================================================
    // Environment
    // ========================================================================

    // Setup HDR environment for IBL
    void setupEnvironment(const std::string& hdrPath);

    // ========================================================================
    // Renderer
    // ========================================================================

    VulkanRenderer* getRenderer() const { return m_Renderer; }

    // ========================================================================
    // Rendering
    // ========================================================================

    // Draw all renderable actors (MiStaticMeshActors, etc.)
    void draw(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& proj, uint32_t frameIndex);

    // ========================================================================
    // Dirty Tracking
    // ========================================================================

    // Override to cascade to actors
    void markDirty() override;
    void clearDirty() override;

    // Check if any actor is dirty
    bool hasUnsavedChanges() const;

    // ========================================================================
    // Serialization
    // ========================================================================

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    // Process pending destruction queue
    void processDestroyQueue();

    // Register/unregister actor
    void registerActor(std::shared_ptr<MiActor> actor);
    void unregisterActor(std::shared_ptr<MiActor> actor);

    // Generate unique actor name
    std::string generateUniqueActorName(const std::string& baseName) const;

    VulkanRenderer* m_Renderer = nullptr;
    // std::unique_ptr<PhysicsWorld> m_PhysicsWorld;

    std::vector<std::shared_ptr<MiActor>> m_Actors;
    std::unordered_map<ObjectId, std::shared_ptr<MiActor>> m_ActorMap;
    std::vector<std::shared_ptr<MiActor>> m_DestroyQueue;
    std::vector<std::shared_ptr<MiActor>> m_SpawnQueue;

    WorldSettings m_Settings;
    std::vector<MiLight> m_Lights;
    bool m_Initialized = false;
    bool m_IsPlaying = false;
    bool m_IsUpdating = false;  // Flag to defer spawn/destroy during tick
};

// ============================================================================
// Template Implementations
// ============================================================================

template<typename T, typename... Args>
std::shared_ptr<T> MiWorld::spawnActor(Args&&... args) {
    static_assert(std::is_base_of<MiActor, T>::value, "T must derive from MiActor");

    auto actor = std::make_shared<T>(std::forward<Args>(args)...);

    // Generate unique name if it's the default
    if (actor->getName() == "Actor" || actor->getName().empty()) {
        actor->setName(generateUniqueActorName(T::StaticTypeName));
    }

    // Create default components
    actor->createDefaultComponents();

    if (m_IsUpdating) {
        // Defer registration until after update
        m_SpawnQueue.push_back(actor);
    } else {
        registerActor(actor);

        // If world is playing, call beginPlay
        if (m_IsPlaying) {
            actor->beginPlay();
        }
    }

    actor->onCreated();
    markDirty();

    return actor;
}

template<typename T>
std::vector<std::shared_ptr<T>> MiWorld::findActorsOfType() const {
    static_assert(std::is_base_of<MiActor, T>::value, "T must derive from MiActor");

    std::vector<std::shared_ptr<T>> result;
    for (const auto& actor : m_Actors) {
        if (auto cast = std::dynamic_pointer_cast<T>(actor)) {
            result.push_back(cast);
        }
    }
    return result;
}

} // namespace MiEngine
