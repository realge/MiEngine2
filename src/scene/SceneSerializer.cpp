#include "scene/SceneSerializer.h"
#include "core/MiWorld.h"
#include "core/MiActor.h"
#include "core/MiComponent.h"
#include "core/MiSceneComponent.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"
#include <fstream>
#include <chrono>

namespace MiEngine {

// ============================================================================
// Timestamp Helper
// ============================================================================

uint64_t SceneSerializer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

// ============================================================================
// Metadata
// ============================================================================

void SceneSerializer::serializeMetadata(const SceneMetadata& metadata, JsonWriter& writer) {
    writer.writeUInt("version", SCENE_FORMAT_VERSION);
    writer.writeString("name", metadata.name);
    writer.writeString("description", metadata.description);
    writer.writeString("author", metadata.author);
    writer.writeUInt64("createdTime", metadata.createdTime);
    writer.writeUInt64("modifiedTime", metadata.modifiedTime);
}

void SceneSerializer::deserializeMetadata(SceneMetadata& metadata, const JsonReader& reader) {
    metadata.version = reader.getUInt("version", 1);
    metadata.name = reader.getString("name", "Untitled");
    metadata.description = reader.getString("description", "");
    metadata.author = reader.getString("author", "");
    metadata.createdTime = reader.getUInt64("createdTime", 0);
    metadata.modifiedTime = reader.getUInt64("modifiedTime", 0);
}

// ============================================================================
// Actor Serialization
// ============================================================================

void SceneSerializer::serializeActor(const MiActor* actor, JsonWriter& writer) {
    if (!actor) return;

    // Let the actor serialize itself (includes type, id, name, etc.)
    actor->serialize(writer);
}

std::shared_ptr<MiActor> SceneSerializer::deserializeActor(MiWorld& world, const JsonReader& reader) {
    // Get type name
    std::string typeName = reader.getString("type", "");
    if (typeName.empty()) {
        return nullptr;
    }

    // Create actor via type registry
    auto& registry = MiTypeRegistry::getInstance();
    auto obj = registry.create(typeName);
    if (!obj) {
        return nullptr;
    }

    auto actor = std::dynamic_pointer_cast<MiActor>(obj);
    if (!actor) {
        return nullptr;
    }

    // Create default components first
    actor->createDefaultComponents();

    // Deserialize actor data (id, name, flags, tags, transform)
    actor->deserialize(reader);

    // Deserialize components
    auto componentReaders = reader.getArray("components");
    for (const auto& compReader : componentReaders) {
        auto component = deserializeComponent(compReader);
        if (component) {
            // Check if this component type already exists from createDefaultComponents
            // For simplicity, we just add all deserialized components
            // A more sophisticated approach would merge with existing components

            component->setOwner(actor.get());

            // If it's a scene component and matches the root, update it
            if (auto sceneComp = std::dynamic_pointer_cast<MiSceneComponent>(component)) {
                auto rootComp = actor->getRootComponent();
                if (rootComp && rootComp->getTypeName() == sceneComp->getTypeName()) {
                    // Update the existing root component instead of adding new
                    rootComp->deserialize(compReader);
                    continue;
                }
            }

            // Add the component if it wasn't the root
            // Note: This is a simplified approach - full implementation would
            // match components by ID or handle component hierarchy properly
        }
    }

    return actor;
}

// ============================================================================
// Component Serialization
// ============================================================================

void SceneSerializer::serializeComponent(const MiComponent* component, JsonWriter& writer) {
    if (!component) return;
    component->serialize(writer);
}

std::shared_ptr<MiComponent> SceneSerializer::deserializeComponent(const JsonReader& reader) {
    std::string typeName = reader.getString("type", "");
    if (typeName.empty()) {
        return nullptr;
    }

    auto& registry = MiTypeRegistry::getInstance();
    auto obj = registry.create(typeName);
    if (!obj) {
        return nullptr;
    }

    auto component = std::dynamic_pointer_cast<MiComponent>(obj);
    if (component) {
        component->deserialize(reader);
    }

    return component;
}

// ============================================================================
// Save Scene
// ============================================================================

SceneResult SceneSerializer::saveScene(const MiWorld& world, const fs::path& filePath) {
    SceneMetadata metadata;
    metadata.name = world.getName();
    metadata.createdTime = getCurrentTimestamp();
    metadata.modifiedTime = getCurrentTimestamp();

    return saveScene(world, filePath, metadata);
}

SceneResult SceneSerializer::saveScene(const MiWorld& world, const fs::path& filePath,
                                        const SceneMetadata& metadata) {
    SceneResult result;

    try {
        JsonWriter writer;
        writer.beginObject();

        // Metadata
        serializeMetadata(metadata, writer);

        // World settings
        writer.beginObject("settings");
        world.getSettings().serialize(writer);
        writer.endObject();

        // Count non-transient actors
        size_t actorCount = 0;
        for (const auto& actor : world.getAllActors()) {
            if (!actor->isTransient()) {
                ++actorCount;
            }
        }

        // Actors
        writer.beginArray("actors");
        for (const auto& actor : world.getAllActors()) {
            // Skip transient actors
            if (actor->isTransient()) {
                continue;
            }

            writer.beginArrayObject();
            serializeActor(actor.get(), writer);
            writer.endObject();
        }
        writer.endArray();

        writer.endObject();

        // Write to file
        if (!writer.saveToFile(filePath.string())) {
            result.success = false;
            result.errorMessage = "Failed to write file: " + filePath.string();
            return result;
        }

        result.success = true;
        result.metadata = metadata;
        result.metadata.modifiedTime = getCurrentTimestamp();
        result.actorCount = actorCount;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Exception during save: ") + e.what();
    }

    return result;
}

// ============================================================================
// Load Scene
// ============================================================================

SceneResult SceneSerializer::loadScene(MiWorld& world, const fs::path& filePath) {
    SceneResult result;

    try {
        // Load JSON
        JsonReader reader;
        if (!reader.loadFromFile(filePath.string())) {
            result.success = false;
            result.errorMessage = "Failed to read file: " + filePath.string();
            return result;
        }

        // Check version
        uint32_t version = reader.getUInt("version", 0);
        if (version == 0 || version > SCENE_FORMAT_VERSION) {
            result.success = false;
            result.errorMessage = "Unsupported scene format version: " + std::to_string(version);
            return result;
        }

        // Metadata
        deserializeMetadata(result.metadata, reader);

        // World settings
        JsonReader settingsReader = reader.getObject("settings");
        if (settingsReader.isValid()) {
            WorldSettings settings;
            settings.deserialize(settingsReader);
            world.setSettings(settings);
        }

        // Clear existing actors
        world.destroyAllActors();

        // Load actors
        auto actorReaders = reader.getArray("actors");
        result.actorCount = 0;

        for (const auto& actorReader : actorReaders) {
            auto actor = deserializeActor(world, actorReader);
            if (actor) {
                // The actor was created via type registry, not spawned
                // We need to manually register it with the world
                // For now, we use a workaround - spawn by type and copy data

                // Actually, let's spawn properly and then update
                std::string typeName = actorReader.getString("type", "");
                auto spawnedActor = world.spawnActorByTypeName(typeName);
                if (spawnedActor) {
                    // Copy the ID to maintain references
                    spawnedActor->setObjectId(actorReader.getString("id", spawnedActor->getObjectId()));
                    spawnedActor->setName(actorReader.getString("name", spawnedActor->getName()));

                    // Deserialize the rest
                    spawnedActor->deserialize(actorReader);

                    ++result.actorCount;
                }
            }
        }

        // Update world name
        world.setName(result.metadata.name);
        world.clearDirty();

        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Exception during load: ") + e.what();
    }

    return result;
}

// ============================================================================
// Peek Scene
// ============================================================================

SceneResult SceneSerializer::peekScene(const fs::path& filePath) {
    SceneResult result;

    try {
        JsonReader reader;
        if (!reader.loadFromFile(filePath.string())) {
            result.success = false;
            result.errorMessage = "Failed to read file: " + filePath.string();
            return result;
        }

        deserializeMetadata(result.metadata, reader);

        // Count actors
        auto actorReaders = reader.getArray("actors");
        result.actorCount = actorReaders.size();

        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Exception during peek: ") + e.what();
    }

    return result;
}

// ============================================================================
// Validation
// ============================================================================

bool SceneSerializer::validateScene(const fs::path& filePath) {
    if (!fs::exists(filePath)) {
        return false;
    }

    try {
        JsonReader reader;
        if (!reader.loadFromFile(filePath.string())) {
            return false;
        }

        // Check for required fields
        if (!reader.hasKey("version")) return false;
        if (!reader.hasKey("actors")) return false;

        uint32_t version = reader.getUInt("version", 0);
        if (version == 0 || version > SCENE_FORMAT_VERSION) {
            return false;
        }

        return true;

    } catch (...) {
        return false;
    }
}

bool SceneSerializer::isSceneFile(const fs::path& filePath) {
    return filePath.extension() == SCENE_FILE_EXTENSION;
}

// ============================================================================
// Default Scene
// ============================================================================

void SceneSerializer::createDefaultScene(MiWorld& world) {
    // Clear existing
    world.destroyAllActors();

    // Set default world settings
    WorldSettings settings;
    settings.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    settings.ambientColor = glm::vec3(0.1f, 0.1f, 0.1f);
    world.setSettings(settings);

    // Create a default empty actor as a placeholder
    // In a full implementation, you might add:
    // - Default camera
    // - Default directional light
    // - Ground plane

    world.setName("NewScene");
    world.clearDirty();
}

} // namespace MiEngine
