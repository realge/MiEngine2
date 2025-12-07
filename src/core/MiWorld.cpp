#include "core/MiWorld.h"
#include "core/MiActor.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"
#include "actor/MiStaticMeshActor.h"
#include "component/MiStaticMeshComponent.h"
#include "mesh/Mesh.h"
#include "scene/Scene.h"
#include "VulkanRenderer.h"
#include <algorithm>
#include <sstream>
#include <iostream>

namespace MiEngine {

// ============================================================================
// WorldSettings
// ============================================================================

void WorldSettings::serialize(JsonWriter& writer) const {
    writer.writeVec3("gravity", gravity);
    writer.writeFloat("physicsTimeStep", physicsTimeStep);
    writer.writeBool("enablePhysics", enablePhysics);
    writer.writeVec3("ambientColor", ambientColor);
    writer.writeString("skybox", skyboxPath);
}

void WorldSettings::deserialize(const JsonReader& reader) {
    gravity = reader.getVec3("gravity", glm::vec3(0.0f, -9.81f, 0.0f));
    physicsTimeStep = reader.getFloat("physicsTimeStep", 1.0f / 60.0f);
    enablePhysics = reader.getBool("enablePhysics", true);
    ambientColor = reader.getVec3("ambientColor", glm::vec3(0.1f, 0.1f, 0.1f));
    skyboxPath = reader.getString("skybox", "");
}

// ============================================================================
// MiWorld
// ============================================================================

MiWorld::MiWorld()
    : MiObject()
    , m_Renderer(nullptr)
    , m_Initialized(false)
    , m_IsPlaying(false)
    , m_IsUpdating(false)
{
    setName("World");
}

MiWorld::~MiWorld() {
    shutdown();
}

void MiWorld::initialize(VulkanRenderer* renderer) {
    if (m_Initialized) {
        return;
    }

    m_Renderer = renderer;

    // Initialize physics world in the future
    // m_PhysicsWorld = std::make_unique<PhysicsWorld>();
    // m_PhysicsWorld->setGravity(m_Settings.gravity);

    m_Initialized = true;
}

void MiWorld::shutdown() {
    if (!m_Initialized) {
        return;
    }

    // End play if still playing
    if (m_IsPlaying) {
        endPlay();
    }

    // Destroy all actors
    destroyAllActors();
    processDestroyQueue();

    // Clean up physics
    // m_PhysicsWorld.reset();

    m_Renderer = nullptr;
    m_Initialized = false;
}

// ============================================================================
// Actor Management
// ============================================================================

std::shared_ptr<MiActor> MiWorld::spawnActorByTypeName(const std::string& typeName) {
    auto& registry = MiTypeRegistry::getInstance();

    auto obj = registry.create(typeName);
    if (!obj) {
        return nullptr;
    }

    auto actor = std::dynamic_pointer_cast<MiActor>(obj);
    if (!actor) {
        return nullptr;
    }

    // Generate unique name
    actor->setName(generateUniqueActorName(typeName));

    // Create default components
    actor->createDefaultComponents();

    if (m_IsUpdating) {
        m_SpawnQueue.push_back(actor);
    } else {
        registerActor(actor);

        if (m_IsPlaying) {
            actor->beginPlay();
        }
    }

    actor->onCreated();
    markDirty();

    return actor;
}

void MiWorld::destroyActor(std::shared_ptr<MiActor> actor) {
    if (!actor) {
        return;
    }

    // Mark for destruction
    actor->addFlags(ActorFlags::Destroying);
    actor->markPendingDestroy();

    // Add to destroy queue
    m_DestroyQueue.push_back(actor);
}

void MiWorld::destroyActor(const ObjectId& id) {
    auto actor = findActorById(id);
    if (actor) {
        destroyActor(actor);
    }
}

void MiWorld::destroyAllActors() {
    for (auto& actor : m_Actors) {
        actor->addFlags(ActorFlags::Destroying);
        actor->markPendingDestroy();
        m_DestroyQueue.push_back(actor);
    }
}

void MiWorld::registerActor(std::shared_ptr<MiActor> actor) {
    if (!actor) {
        return;
    }

    actor->setWorld(this);
    m_Actors.push_back(actor);
    m_ActorMap[actor->getObjectId()] = actor;

    // Notify actor - good time for components to load resources
    actor->onRegister();
}

void MiWorld::unregisterActor(std::shared_ptr<MiActor> actor) {
    if (!actor) {
        return;
    }

    // Call endPlay if playing
    if (m_IsPlaying && actor->hasBegunPlay()) {
        actor->endPlay();
    }

    // Notify actor - components can unload resources
    actor->onUnregister();

    // Call onDestroyed
    actor->onDestroyed();

    // Remove from map
    m_ActorMap.erase(actor->getObjectId());

    // Remove from vector
    auto it = std::find(m_Actors.begin(), m_Actors.end(), actor);
    if (it != m_Actors.end()) {
        m_Actors.erase(it);
    }

    actor->setWorld(nullptr);
}

void MiWorld::processDestroyQueue() {
    for (auto& actor : m_DestroyQueue) {
        unregisterActor(actor);
    }
    m_DestroyQueue.clear();

    // Process spawn queue
    for (auto& actor : m_SpawnQueue) {
        registerActor(actor);
        if (m_IsPlaying) {
            actor->beginPlay();
        }
    }
    m_SpawnQueue.clear();
}

std::string MiWorld::generateUniqueActorName(const std::string& baseName) const {
    std::string name = baseName;
    int counter = 1;

    while (findActorByName(name) != nullptr) {
        std::ostringstream ss;
        ss << baseName << "_" << counter++;
        name = ss.str();
    }

    return name;
}

// ============================================================================
// Actor Queries
// ============================================================================

std::shared_ptr<MiActor> MiWorld::findActorById(const ObjectId& id) const {
    auto it = m_ActorMap.find(id);
    if (it != m_ActorMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<MiActor> MiWorld::findActorByName(const std::string& name) const {
    for (const auto& actor : m_Actors) {
        if (actor->getName() == name) {
            return actor;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<MiActor>> MiWorld::findActorsByTag(const std::string& tag) const {
    std::vector<std::shared_ptr<MiActor>> result;
    for (const auto& actor : m_Actors) {
        if (actor->hasTag(tag)) {
            result.push_back(actor);
        }
    }
    return result;
}

std::vector<std::shared_ptr<MiActor>> MiWorld::findActorsByLayer(uint32_t layer) const {
    std::vector<std::shared_ptr<MiActor>> result;
    for (const auto& actor : m_Actors) {
        if (actor->getLayer() == layer) {
            result.push_back(actor);
        }
    }
    return result;
}

// ============================================================================
// Iteration
// ============================================================================

void MiWorld::forEachActor(const std::function<void(MiActor*)>& callback) {
    for (auto& actor : m_Actors) {
        if (!actor->isPendingDestroy()) {
            callback(actor.get());
        }
    }
}

void MiWorld::forEachActor(const std::function<void(const MiActor*)>& callback) const {
    for (const auto& actor : m_Actors) {
        if (!actor->isPendingDestroy()) {
            callback(actor.get());
        }
    }
}

// ============================================================================
// Update Loop
// ============================================================================

void MiWorld::beginPlay() {
    if (m_IsPlaying) {
        return;
    }

    m_IsPlaying = true;

    // Call beginPlay on all actors
    for (auto& actor : m_Actors) {
        if (!actor->isPendingDestroy()) {
            actor->beginPlay();
        }
    }
}

void MiWorld::endPlay() {
    if (!m_IsPlaying) {
        return;
    }

    // Call endPlay on all actors
    for (auto& actor : m_Actors) {
        if (actor->hasBegunPlay()) {
            actor->endPlay();
        }
    }

    m_IsPlaying = false;
}

void MiWorld::tick(float deltaTime) {
    if (!m_Initialized) {
        return;
    }

    m_IsUpdating = true;

    // Update physics (future)
    // if (m_Settings.enablePhysics && m_PhysicsWorld) {
    //     m_PhysicsWorld->update(deltaTime);
    // }

    // Tick all actors
    for (auto& actor : m_Actors) {
        if (!actor->isPendingDestroy() && actor->hasBegunPlay()) {
            actor->tick(deltaTime);
        }
    }

    m_IsUpdating = false;

    // Process deferred spawn/destroy
    processDestroyQueue();
}

// ============================================================================
// Rendering
// ============================================================================

void MiWorld::draw(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& proj, uint32_t frameIndex) {
    if (!m_Renderer || !m_Initialized) {
        return;
    }

    // Check which pipeline to use
    bool usePBR = m_Renderer->getRenderMode() == RenderMode::PBR ||
                  m_Renderer->getRenderMode() == RenderMode::PBR_IBL;

    // Find all static mesh actors
    auto staticMeshActors = findActorsOfType<MiStaticMeshActor>();

    for (auto& actor : staticMeshActors) {
        if (actor->isPendingDestroy()) {
            continue;
        }

        auto meshComponent = actor->getMeshComponent();
        if (!meshComponent || !meshComponent->shouldRender()) {
            continue;
        }

        auto mesh = meshComponent->getMesh();
        if (!mesh) {
            continue;
        }

        // Get material from component (not mesh - component has per-instance material)
        Material& material = meshComponent->getMaterial();

        // Create descriptor set for material if not exists
        if (material.getDescriptorSet() == VK_NULL_HANDLE) {
            VkDescriptorSet ds = m_Renderer->createMaterialDescriptorSet(material);
            if (ds != VK_NULL_HANDLE) {
                material.setDescriptorSet(ds);
            } else {
                std::cerr << "MiWorld: Failed to create material descriptor set for actor: "
                          << actor->getName() << std::endl;
                continue;
            }
        }

        // Get the model matrix from actor transform
        glm::mat4 model = actor->getTransform().getMatrix();

        if (usePBR) {
            // Push the model matrix as a push constant
            PushConstant pushConstant = m_Renderer->createPushConstant(model, material);
            vkCmdPushConstants(
                commandBuffer,
                m_Renderer->getPBRPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstant),
                &pushConstant
            );

            // Bind material descriptor set (set 1)
            VkDescriptorSet materialDescriptorSet = material.getDescriptorSet();
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_Renderer->getPBRPipelineLayout(),
                    1,  // Set index 1
                    1,
                    &materialDescriptorSet,
                    0, nullptr
                );
            }

            // Draw the mesh
            mesh->bind(commandBuffer);
            mesh->draw(commandBuffer);
            m_Renderer->addDrawCall(0, mesh->indexCount);
        } else {
            // Standard pipeline
            PushConstant pushConstant = m_Renderer->createPushConstant(model, material);
            vkCmdPushConstants(
                commandBuffer,
                m_Renderer->getPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(PushConstant),
                &pushConstant
            );

            // Bind material descriptor set
            VkDescriptorSet materialDescriptorSet = material.getDescriptorSet();
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_Renderer->getPipelineLayout(),
                    1,  // Set index 1
                    1,
                    &materialDescriptorSet,
                    0, nullptr
                );
            }

            // Draw the mesh
            mesh->bind(commandBuffer);
            mesh->draw(commandBuffer);
            m_Renderer->addDrawCall(0, mesh->indexCount);
        }
    }
}

// ============================================================================
// Lighting
// ============================================================================

void MiWorld::addLight(const glm::vec3& positionOrDirection, const glm::vec3& color,
                       float intensity, float radius, float falloff, bool isDirectional) {
    MiLight light;
    light.position = positionOrDirection;
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    light.falloff = falloff;
    light.isDirectional = isDirectional;

    m_Lights.push_back(light);

    // Sync with renderer's legacy scene for now (lights are still managed there)
    if (m_Renderer && m_Renderer->getScene()) {
        m_Renderer->getScene()->addLight(positionOrDirection, color, intensity, radius, falloff, isDirectional);
    }
    //TODO: implement light system for Mi world

    markDirty();
}

void MiWorld::addLight(const MiLight& light) {
    addLight(light.position, light.color, light.intensity, light.radius, light.falloff, light.isDirectional);
}

void MiWorld::removeLight(size_t index) {
    if (index < m_Lights.size()) {
        m_Lights.erase(m_Lights.begin() + index);

        // Sync with renderer's legacy scene
        if (m_Renderer && m_Renderer->getScene()) {
            m_Renderer->getScene()->removeLight(index);
        }

        markDirty();
    }
}

void MiWorld::clearLights() {
    m_Lights.clear();

    // Sync with renderer's legacy scene
    if (m_Renderer && m_Renderer->getScene()) {
        m_Renderer->getScene()->clearLights();
    }

    markDirty();
}

void MiWorld::setupDefaultLighting() {
    clearLights();

    // Add a directional light (sun)
    addLight(
        glm::vec3(-0.5f, -1.0f, -0.3f),  // Direction
        glm::vec3(1.0f, 0.95f, 0.9f),     // Warm sunlight
        2.0f,                              // Intensity
        0.0f,                              // Radius (0 for directional)
        1.0f,                              // Falloff
        true                               // isDirectional
    );

    // Add a soft fill light
    addLight(
        glm::vec3(0.3f, -0.5f, 0.5f),     // Direction (opposite side)
        glm::vec3(0.6f, 0.7f, 0.9f),      // Cool blue-ish fill
        0.5f,                              // Lower intensity
        0.0f,
        1.0f,
        true
    );
}

// ============================================================================
// Environment
// ============================================================================

void MiWorld::setupEnvironment(const std::string& hdrPath) {
    m_Settings.environmentHDR = hdrPath;

    // Setup environment in renderer's legacy scene (IBL system is there)
    if (m_Renderer && m_Renderer->getScene()) {
        m_Renderer->getScene()->setupEnvironment(hdrPath);
    }

    markDirty();
}

// ============================================================================
// Dirty Tracking
// ============================================================================

void MiWorld::markDirty() {
    MiObject::markDirty();
}

void MiWorld::clearDirty() {
    MiObject::clearDirty();

    for (auto& actor : m_Actors) {
        actor->clearDirty();
    }
}

bool MiWorld::hasUnsavedChanges() const {
    if (isDirty()) {
        return true;
    }

    for (const auto& actor : m_Actors) {
        if (actor->isDirty()) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Serialization
// ============================================================================

void MiWorld::serialize(JsonWriter& writer) const {
    MiObject::serialize(writer);

    // World settings
    writer.beginObject("settings");
    m_Settings.serialize(writer);
    writer.endObject();

    // Actors
    writer.beginArray("actors");
    for (const auto& actor : m_Actors) {
        // Skip transient actors
        if (actor->isTransient()) {
            continue;
        }

        writer.beginArrayObject();
        actor->serialize(writer);
        writer.endObject();
    }
    writer.endArray();
}

void MiWorld::deserialize(const JsonReader& reader) {
    MiObject::deserialize(reader);

    // World settings
    JsonReader settingsReader = reader.getObject("settings");
    if (settingsReader.isValid()) {
        m_Settings.deserialize(settingsReader);
    }

    // Actors are deserialized by SceneSerializer
    // This just provides the structure
}

// Register the type
MI_REGISTER_TYPE(MiWorld)

} // namespace MiEngine
