#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <cstdint>

namespace fs = std::filesystem;

namespace MiEngine {

// Forward declarations
class MiWorld;
class MiActor;
class MiComponent;
class JsonWriter;
class JsonReader;

// Scene file metadata
struct SceneMetadata {
    std::string name;
    std::string description;
    std::string author;
    uint32_t version = 1;
    uint64_t createdTime = 0;
    uint64_t modifiedTime = 0;
};

// Result of scene operations
struct SceneResult {
    bool success = false;
    std::string errorMessage;
    SceneMetadata metadata;
    size_t actorCount = 0;
};

// Scene format version
constexpr uint32_t SCENE_FORMAT_VERSION = 1;

// Scene file extension
constexpr const char* SCENE_FILE_EXTENSION = ".miscene";

// Scene serialization/deserialization
class SceneSerializer {
public:
    // ========================================================================
    // Save Operations
    // ========================================================================

    // Save world to .miscene file
    static SceneResult saveScene(const MiWorld& world, const fs::path& filePath);

    // Save world with custom metadata
    static SceneResult saveScene(const MiWorld& world, const fs::path& filePath,
                                  const SceneMetadata& metadata);

    // ========================================================================
    // Load Operations
    // ========================================================================

    // Load world from .miscene file
    static SceneResult loadScene(MiWorld& world, const fs::path& filePath);

    // Get metadata without loading full scene
    static SceneResult peekScene(const fs::path& filePath);

    // ========================================================================
    // Validation
    // ========================================================================

    // Validate scene file format
    static bool validateScene(const fs::path& filePath);

    // Check if file is a scene file
    static bool isSceneFile(const fs::path& filePath);

    // ========================================================================
    // Utility
    // ========================================================================

    // Create empty scene with default actors (camera, light)
    static void createDefaultScene(MiWorld& world);

    // Get current timestamp
    static uint64_t getCurrentTimestamp();

private:
    // Internal serialization helpers
    static void serializeMetadata(const SceneMetadata& metadata, JsonWriter& writer);
    static void deserializeMetadata(SceneMetadata& metadata, const JsonReader& reader);

    static void serializeActor(const MiActor* actor, JsonWriter& writer);
    static std::shared_ptr<MiActor> deserializeActor(MiWorld& world, const JsonReader& reader);

    static void serializeComponent(const MiComponent* component, JsonWriter& writer);
    static std::shared_ptr<MiComponent> deserializeComponent(const JsonReader& reader);
};

} // namespace MiEngine
